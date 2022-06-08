#define _GNU_SOURCE
#include <stdio.h>
#include <sys/mman.h>
#include <pthread.h>

#include "utils.h"
#include "list.h"
#include "ubbd_backend.h"
#include "ubbd_uio.h"
#include "ubbd_netlink.h"
#include "ubbd_queue.h"


struct ubbd_rbd_backend *create_rbd_backend(void)
{
	struct ubbd_backend *ubbd_b;
	struct ubbd_rbd_backend *dev;

	dev = calloc(1, sizeof(*dev));
	if (!dev)
		return NULL;

	ubbd_b = &dev->ubbd_b;
	ubbd_b->dev_type = UBBD_DEV_TYPE_RBD;
	ubbd_b->backend_ops = &rbd_backend_ops;

	return dev;
}

struct ubbd_null_backend *create_null_backend(void)
{
	struct ubbd_backend *ubbd_b;
	struct ubbd_null_backend *dev;

	dev = calloc(1, sizeof(*dev));
	if (!dev)
		return NULL;

	ubbd_b = &dev->ubbd_b;
	ubbd_b->dev_type = UBBD_DEV_TYPE_NULL;
	ubbd_b->backend_ops = &null_backend_ops;

	return dev;
}

struct ubbd_file_backend *create_file_backend(void)
{
	struct ubbd_backend *ubbd_b;
	struct ubbd_file_backend *dev;

	dev = calloc(1, sizeof(*dev));
	if (!dev)
		return NULL;

	ubbd_b = &dev->ubbd_b;
	ubbd_b->dev_type = UBBD_DEV_TYPE_FILE;
	ubbd_b->backend_ops = &file_backend_ops;

	return dev;
}

static int ubbd_backend_init(struct ubbd_backend *ubbd_b, struct ubbd_backend_conf *conf)
{
	int ret;
	struct ubbd_queue *ubbd_q;
	int i;

	ubbd_b->num_queues = conf->num_queues;
	ubbd_b->status = UBBD_BACKEND_STATUS_INIT;

	ubbd_b->queues = calloc(ubbd_b->num_queues, sizeof(struct ubbd_queue));
	if (!ubbd_b->queues) {
		ubbd_err("failed to alloc queues\n");
		ret = -ENOMEM;
		goto out;
	}


	for (i = 0; i < ubbd_b->num_queues; i++) {
		ubbd_q = &ubbd_b->queues[i];
		ubbd_q->ubbd_b = ubbd_b;
		ubbd_q->uio_info.uio_id = conf->queue_infos[i].uio_id;
		ubbd_q->uio_info.uio_map_size = conf->queue_infos[i].uio_map_size;
		ubbd_q->backend_pid = conf->queue_infos[i].backend_pid;
		ubbd_q->status = conf->queue_infos[i].status;
		memcpy(&ubbd_q->cpuset, &conf->queue_infos[i].cpuset, sizeof(cpu_set_t));
		ubbd_q->index = i;
	}

	return 0;

out:
	return ret;
}

struct ubbd_backend *ubbd_backend_create(struct ubbd_backend_conf *conf)
{
	struct ubbd_backend *ubbd_b;
	struct ubbd_dev_info *dev_info = &conf->dev_info;
	int ret;

	if (dev_info->type == UBBD_DEV_TYPE_FILE) {
		struct ubbd_file_backend *file_backend;

		file_backend = create_file_backend();
		if (!file_backend)
			return NULL;
		ubbd_b = &file_backend->ubbd_b;
		strcpy(file_backend->filepath, dev_info->file.path);
	} else if (dev_info->type == UBBD_DEV_TYPE_RBD) {
		struct ubbd_rbd_backend *rbd_backend;

		rbd_backend = create_rbd_backend();
		if (!rbd_backend)
			return NULL;
		ubbd_b = &rbd_backend->ubbd_b;
		strcpy(rbd_backend->pool, dev_info->rbd.pool);
		strcpy(rbd_backend->imagename, dev_info->rbd.image);
	} else if (dev_info->type == UBBD_DEV_TYPE_NULL){
		struct ubbd_null_backend *null_backend;

		null_backend = create_null_backend();
		if (!null_backend)
			return NULL;
		ubbd_b = &null_backend->ubbd_b;
	}else {
		ubbd_err("Unknown dev type\n");
		return NULL;
	}

	ubbd_err("before init\n");
	ubbd_b->dev_id = conf->dev_id;
	memcpy(&ubbd_b->dev_info, dev_info, sizeof(struct ubbd_dev_info));

	ret = ubbd_backend_init(ubbd_b, conf);
	if (ret) {
		ubbd_err("failed to init backend\n");
		goto err_release;
	}

	return ubbd_b;

err_release:
	ubbd_backend_release(ubbd_b);

	return NULL;
}

int ubbd_backend_open(struct ubbd_backend *ubbd_b)
{
	return ubbd_b->backend_ops->open(ubbd_b);
}

void ubbd_backend_close(struct ubbd_backend *ubbd_b)
{
	ubbd_b->backend_ops->close(ubbd_b);
}

void ubbd_backend_release(struct ubbd_backend *ubbd_b)
{
	ubbd_b->backend_ops->release(ubbd_b);
}

int ubbd_backend_start(struct ubbd_backend *ubbd_b, bool start_queues)
{
	struct ubbd_queue *ubbd_q;
	int ret = 0;
	int i;

	if (start_queues) {
		for (i = 0; i < ubbd_b->num_queues; i++) {
			ubbd_q = &ubbd_b->queues[i];
			ubbd_q->ubbd_b = ubbd_b;
			pthread_mutex_init(&ubbd_q->req_lock, NULL);
			pthread_mutex_init(&ubbd_q->req_stats_lock, NULL);
			ret = ubbd_queue_setup(ubbd_q);
			if (ret)
				goto out;
		}
	}

	ubbd_b->status = UBBD_BACKEND_STATUS_RUNNING;

out:
	return ret;
}

void ubbd_backend_stop(struct ubbd_backend *ubbd_b)
{
	struct ubbd_queue *ubbd_q;
	int i;

	for (i = 0; i < ubbd_b->num_queues; i++) {
		ubbd_q = &ubbd_b->queues[i];
		ubbd_queue_stop(ubbd_q);
	}
}

void ubbd_backend_wait_stopped(struct ubbd_backend *ubbd_b)
{
	struct ubbd_queue *ubbd_q;
	int i;

	for (i = 0; i < ubbd_b->num_queues; i++) {
		ubbd_q = &ubbd_b->queues[i];
		ubbd_queue_wait_stopped(ubbd_q);
	}
}

int ubbd_backend_stop_queue(struct ubbd_backend *ubbd_b, int queue_id)
{
	struct ubbd_queue *ubbd_q;

	if (queue_id >= ubbd_b->num_queues) {
		ubbd_err("queue_id is invalid: %d\n", queue_id);
		return -EINVAL;
	}
	ubbd_q = &ubbd_b->queues[queue_id];
	ubbd_queue_stop(ubbd_q);

	return 0;
}

int ubbd_backend_start_queue(struct ubbd_backend *ubbd_b, int queue_id)
{
	struct ubbd_queue *ubbd_q;

	if (queue_id >= ubbd_b->num_queues) {
		ubbd_err("queue_id is invalid: %d\n", queue_id);
		return -EINVAL;
	}
	ubbd_q = &ubbd_b->queues[queue_id];
	ubbd_queue_setup(ubbd_q);

	return 0;
}