/* Internal, streaming text commands. No shell, subprocesses or file writes. */
#ifndef MIJOE_VIMPIPE_H
#define MIJOE_VIMPIPE_H
#define VP_LINE_LIMIT (1024L * 1024L)
struct vim_pipeline;
int vp_has_pipe(const char *text);
struct vim_pipeline *vp_compile(const char *text, const char **error);
void vp_free(struct vim_pipeline *pipeline);
const char *vp_error(struct vim_pipeline *pipeline);
const char *vp_title(struct vim_pipeline *pipeline);
int vp_to_file(struct vim_pipeline *pipeline);
int vp_run(struct vim_pipeline *pipeline, BW *source,
           int (*emit)(JOE_LONG_LONG, JOE_LONG_LONG, const char *, void *), void *context);
#endif
