/*
 * Copyright © 2019 Manuel Stoeckl
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial
 * portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT.  IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "util.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "test-proto-defs.h"
#include "util.h"

/* from parsing.c */
bool size_check(const struct msg_data *data, const uint32_t *payload,
		unsigned int true_length, int fd_length);

void do_xtype_req_blue(struct context *ctx, const char *interface,
		uint32_t version, struct wp_object *id, int b, int32_t c,
		uint32_t d, struct wp_object *e, const char *f, uint32_t g)
{
	char buf[256];
	sprintf(buf, "%s %u %u %d %d %d %u %s %d", interface, version,
			id ? id->obj_id : 0, b, c, d, e ? e->obj_id : 0, f, g);
	printf("%s\n", buf);
	ctx->drop_this_msg =
			strcmp(buf, "babacba 4441 992 7771 3331 4442 991 (null) 4443") !=
			0;
}
void do_xtype_evt_yellow(struct context *ctx, uint32_t c)
{
	char buf[256];
	sprintf(buf, "%u", c);
	printf("%s\n", buf);
	ctx->drop_this_msg = strcmp(buf, "4441") != 0;
}
void do_ytype_req_green(struct context *ctx, uint32_t a, const char *b,
		const char *c, int d, const char *e, struct wp_object *f,
		int g_count, const uint8_t *g_val)
{
	char buf[256];
	sprintf(buf, "%u %s %s %d %s %u %d %x|%x|%x|%x|%x|%x|%x|%x", a, b, c, d,
			e, f ? f->obj_id : 0, g_count, g_val[0], g_val[1],
			g_val[2], g_val[3], g_val[4], g_val[5], g_val[6],
			g_val[7]);
	printf("%s\n", buf);
	ctx->drop_this_msg =
			strcmp(buf, "4441 bea (null) 7771 cbbc 991 8 81|80|81|80|90|99|99|99") !=
			0;
}
void do_ytype_evt_red(struct context *ctx, struct wp_object *a, int32_t b,
		int c, struct wp_object *d, int32_t e, int32_t f,
		struct wp_object *g, int32_t h, uint32_t i, const char *j,
		int k, int l_count, const uint8_t *l_val, uint32_t n,
		const char *m, struct wp_object *o, int p, struct wp_object *q)
{
	char buf[256];
	sprintf(buf, "%u %d %d %u %d %d %u %d %u %s %d %d %x|%x|%x %u %s %u %d %u",
			a ? a->obj_id : 0, b, c, d ? d->obj_id : 0, e, f,
			g ? g->obj_id : 0, h, i, j, k, l_count, l_val[0],
			l_val[1], l_val[2], n, m, o ? o->obj_id : 0, p,
			q ? q->obj_id : 0);
	printf("%s\n", buf);
	ctx->drop_this_msg =
			strcmp(buf, "0 33330 8881 0 33331 33332 0 33333 44440 bcaba 8882 3 80|80|80 99990 (null) 992 8883 991") !=
			0;
}

struct wire_test {
	const wp_callfn_t func;
	const struct msg_data *data;
	int fds[4];
	uint32_t words[50];
	int nfds;
	int nwords;
};

log_handler_func_t log_funcs[2] = {test_log_handler, test_log_handler};
int main(int argc, char **argv)
{
	(void)argc;
	(void)argv;

	struct message_tracker mt;
	init_message_tracker(&mt);
	struct wp_object *old_display = listset_get(&mt.objects, 1);
	listset_remove(&mt.objects, old_display);
	destroy_wp_object(NULL, old_display);

	struct fd_translation_map map;
	setup_translation_map(&map, false, COMP_NONE, 1);

	struct wp_object xobj;
	xobj.type = &intf_xtype;
	xobj.is_zombie = false;
	xobj.obj_id = 991;
	listset_insert(&map, &mt.objects, &xobj);

	struct wp_object yobj;
	yobj.type = &intf_ytype;
	yobj.is_zombie = false;
	yobj.obj_id = 992;
	listset_insert(&map, &mt.objects, &yobj);

	struct context ctx = {.obj = &xobj, .g = NULL};

	struct wire_test tests[] = {
			{call_xtype_req_blue, &intf_xtype.funcs[0][0], {7771},
					{8, 0x61626162, 0x00616263, 4441,
							yobj.obj_id, 3331, 4442,
							xobj.obj_id, 0, 4443},
					1, 10},
			{call_xtype_evt_yellow, &intf_xtype.funcs[1][0], {0},
					{4441}, 0, 1},
			{call_ytype_req_green, &intf_ytype.funcs[0][0], {7771},
					{4441, 4, 0x00616562, 0, 5, 0x63626263,
							0x99999900, xobj.obj_id,
							8, 0x80818081,
							0x99999990},
					1, 11},
			{call_ytype_evt_red, &intf_ytype.funcs[1][0],
					{8881, 8882, 8883},
					{7770, 33330, 7771, 33331, 33332, 7773,
							33333, 44440, 6,
							0x62616362, 0x99990061,
							3, 0x11808080, 99990, 0,
							yobj.obj_id,
							xobj.obj_id},
					3, 17}};

	bool all_success = true;
	for (size_t t = 0; t < sizeof(tests) / sizeof(tests[0]); t++) {
		struct wire_test *wt = &tests[t];

		ctx.drop_this_msg = false;
		(*wt->func)(&ctx, wt->words, wt->fds, &mt);
		if (ctx.drop_this_msg) {
			all_success = false;
		}
		printf("Function call %s, %s\n", wt->data->name,
				ctx.drop_this_msg ? "FAIL" : "pass");

		for (int fdlen = wt->nfds; fdlen >= 0; fdlen--) {
			for (int length = wt->nwords; length >= 0; length--) {
				if (fdlen != wt->nfds && length < wt->nwords) {
					/* the fd check is really trivial */
					continue;
				}

				bool expect_success = (wt->nwords == length) &&
						      (fdlen == wt->nfds);
				printf("Trying: %d/%d words, %d/%d fds\n",
						length, wt->nwords, fdlen,
						wt->nfds);

				bool sp = size_check(wt->data, wt->words,
						length, fdlen);
				if (sp != expect_success) {
					wp_log(WP_ERROR,
							"size check FAIL (%c, expected %c) at %d/%d chars, %d/%d fds",
							sp ? 'Y' : 'n',
							expect_success ? 'Y'
								       : 'n',
							length, wt->nwords,
							fdlen, wt->nfds);
				}
				all_success &= (sp == expect_success);
			}
		}
	}

	listset_remove(&mt.objects, &xobj);
	listset_remove(&mt.objects, &yobj);
	cleanup_message_tracker(&map, &mt);
	cleanup_translation_map(&map);

	printf("Net result: %s\n", all_success ? "pass" : "FAIL");
	return all_success ? EXIT_SUCCESS : EXIT_FAILURE;
}