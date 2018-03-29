#include "common.h"
#include <sys/types.h>
#include <linux/types.h>
#include <signal.h>
#include <errno.h>
#include <arpa/inet.h>

#include "ops.h"

int stopped;

struct qe {
	struct xp_qe		*ctx;
};

struct endpoint {
	struct xp_ep		*ctx;
	__u8			 state;
	__u64			 depth;
};

struct listener {
	struct xp_pep		*ctx;
	__u8			 state;
};


void signal_handler(int ev)
{
	exit(0);
}

int wait_for_msg(struct endpoint *ep, struct qe *qe, void **msg,
		 int *bytes, struct xp_ops *ops)
{ 
	int ret;

	do {
		ret = ops->poll_for_msg(ep->ctx, &qe->ctx, msg, bytes);
		if (ret && ret != -EAGAIN) {
			printf("poll_for_msg returned %d\n", ret);
			goto out;
		}
	} while (ret == -EAGAIN);
out:
	return ret;
}

void act_as_server(struct xp_ops *ops, void *cmd, void *data, int size)
{
	int			 ret;
	struct listener		 pep = { 0 };
	struct endpoint		 ep = { 0 };
	struct qe		 qe;
	void			*id;
	int			 bytes = 5;
	char			*srvc = "22666";
	int			 depth = 3;
	void			*req = "hello";
	char			*expect;
	u64			 addr = 0;
	u32			 key, len;
	void			*msg;
	struct xp_mr		*mr = NULL, *data_mr;

	ret = ops->init_listener(&pep.ctx, srvc);
	if (ret) {
		print_err("init_listener returned %d", ret);
		return;
	}

	while (!stopped) {
		ret = ops->wait_for_connection(pep.ctx, &id);
		if (ret) {
			print_err("wait_for_connection returned %d", ret);
			break;
		}

		ret = ops->create_endpoint(&ep.ctx, id, depth);
		if (ret) {
			print_err("create_endpoint returned %d", ret);
			break;
		}

		ret = ops->accept_connection(ep.ctx);
		if (ret) {
			print_err("accept_connection returned %d", ret);
			break;
		}

		ret = ops->alloc_key(ep.ctx, cmd, size, &mr);
		if (ret) {
			print_err("alloc_key returned %d", ret);
			break;
		}

		ret = ops->alloc_key(ep.ctx, data, size, &data_mr);
		if (ret) {
			print_err("alloc_key returned %d", ret);
			break;
		}

		sprintf(cmd, "hello");
		ops->send_msg(ep.ctx, cmd, 5, mr);

		ret = wait_for_msg(&ep, &qe, &msg, &bytes, ops);
		if (ret) {
			printf("wait_for_msg returned %d\n", ret);
			break;
		}

		if ((bytes != 5) || strncmp(msg, "HELLO", 5)) {
			printf("unexpected: '%s`\n", (char *) msg);
			break;
		}

		ret = wait_for_msg(&ep, &qe, &msg, &bytes, ops);

		if (strncmp(msg, "READ ", 5)) {
			printf("unexpected: '%s`\n", (char *) msg);
			break;
		}

		expect = index(msg, ' ');
		addr = atoi(++expect);

		expect = index(expect, ' ');
		key = atoi(++expect);

		expect = index(expect, ' ');
		len = strlen(++expect);

		ret = ops->rma_read(ep.ctx, data, addr, size, key, data_mr);
		if (ret) {
			printf("rma_read returned %d\n", ret);
			break;
		}

		if (strncmp(data, expect, len)) {
			printf("data '%s' != '%s'\n", data, expect);
			ret = -EBADE;
			break;
		}

		len = sprintf(data, "!! WROTE THIS !!");
		ret = ops->rma_write(ep.ctx, data, addr, size, key, data_mr);
		if (ret) {
			printf("rma_read returned %d\n", ret);
			break;
		}

		sprintf(cmd, "done.");
		ops->send_msg(ep.ctx, cmd, 5, mr);

		stopped = 1;
	}

	if (ret)
		printf("server unit test failed %d\n", ret);
	else
		printf("server unit test passed\n");

	if (mr)
		ops->dealloc_key(mr);
	if (data_mr)
		ops->dealloc_key(data_mr);

	ops->destroy_endpoint(ep.ctx);
	ops->destroy_listener(pep.ctx);
}

void act_as_client(struct xp_ops *ops, void *cmd, void *data, int size)
{
	int			 ret;
	struct endpoint		 ep = { 0 };
	struct qe		 qe;
	void			*id;
	int			 bytes = 5;
	char			*srvc = "22666";
	int			 depth = 1;
	void			*req = "hello";
	char			*expect;
	u64			 addr = 0;
	u32			 key, len;
	void			*msg;
	struct xp_mr		*mr = NULL, *data_mr;
	struct sockaddr		 dest = { 0 };
	struct sockaddr_in	*dest_in = (struct sockaddr_in *) &dest;

	dest_in->sin_port = htons(atoi(srvc));
	dest_in->sin_family = AF_INET;
	inet_pton(AF_INET, "192.168.22.2", &dest_in->sin_addr);

	ret = ops->init_endpoint(&ep.ctx, depth);
	if (ret) {
		print_err("init_endpoint returned %d", ret);
		return;
	}

	//bytes = build_connect_data(&req);

	ret = ops->client_connect(ep.ctx, &dest, req, bytes);
	if (ret) {
		printf("client connect failed %d\n", -ret);
		goto out;
	}

	ret = ops->alloc_key(ep.ctx, cmd, size, &mr);
	if (ret)
		goto out;

	ret = wait_for_msg(&ep, &qe, &msg, &bytes, ops);
	if (ret) {
		printf("wait_for_msg returned %d\n", ret);
		goto out;
	}

	if ((bytes != 5) || strncmp(msg, "hello", 5)) {
		printf("unexpected: '%s`\n", (char *) msg);
		goto out;
	}

	sprintf(cmd, "HELLO");
	ops->send_msg(ep.ctx, cmd, 5, mr);

	sprintf(data, "!! READ THIS !!");

	ret = ops->alloc_key(ep.ctx, data, size, &data_mr);
	if (ret)
		goto out2;

	len = sprintf(cmd, "READ %u %u %s",
		      data, ops->remote_key(data_mr), data);

	ops->send_msg(ep.ctx, cmd, len, mr);

	ret = wait_for_msg(&ep, &qe, &msg, &bytes, ops);
	if (ret) {
		printf("wait_for_msg returned %d\n", ret);
		goto out3;
	}

	if (strcmp(data, "!! WROTE THIS !!")) {
		printf("unexpected: '%s'\n", data);
		goto out3;
	}

	printf("client unit test passed\n");

	stopped = 1;

out3:
	ops->dealloc_key(data_mr);
out2:
	ops->dealloc_key(mr);
out:
	ops->destroy_endpoint(ep.ctx);
}

int main(int argc, char *argv)
{
	struct xp_ops		*ops;
	void			*cmd;
	void			*data;
	int			 size = PAGE_SIZE;

	ops = rdma_register_ops();

	if (posix_memalign(&cmd, PAGE_SIZE, size)) {
                print_err("no memory for command buffer, errno %d", errno);
                return -1;
        }
        memset(cmd, 0, size);

	if (posix_memalign(&data, PAGE_SIZE, size)) {
                print_err("no memory for data buffer, errno %d", errno);
                return -1;
        }
        memset(data, 0, size);

	signal(SIGINT, signal_handler);

	stopped = 0;

	if (argc > 1)
		act_as_server(ops, cmd, data, size);
	else
		act_as_client(ops, cmd, data, size);

	free(cmd);
	free(data);
	return 0;
}
