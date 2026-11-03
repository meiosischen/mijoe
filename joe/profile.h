#ifndef MIJOE_PROFILE_H
#define MIJOE_PROFILE_H
/* Returned paths are JOE temporary strings. MIJOE_CONFIG_HOME overrides ~/.mj. */
char *mj_profile_path(const char *name, int create);
char *mj_profile_asset(const char *group, const char *name, const char *extension);
void mj_profile_migrate(void);
int mj_profile_local_options(OPTIONS *options);
void mj_profile_configure(void);
int mj_profile_replace(const char *temporary, const char *target);
#endif
