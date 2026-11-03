/* Personal settings live in one directory. Legacy files are copied, not moved. */
#include "types.h"
#ifndef JOEWIN
#include <dirent.h>
#endif

char *mj_profile_path(const char *name, int create)
{
    const char *override=getenv("MIJOE_CONFIG_HOME"), *home=getenv("HOME");
    char *root;
    struct stat status;
    if (override && *override) root=vsdupz(override);
    else if (home && *home) root=vsfmt(NULL,0,"%s/.mj",home);
    else return NULL;
    if (create && stat(root,&status)) {
#ifdef JOEWIN
        if (mkdir(root,0700) && errno!=EEXIST) return NULL;
#else
        if (mkdir(root,0700) && errno!=EEXIST) return NULL;
#endif
    }
    return *name ? vsfmt(NULL,0,"%s/%s",root,name) : root;
}
static void copy_missing(const char *source, const char *target)
{
    FILE *in,*out;
    char bytes[4096];
    size_t count;
    struct stat status;
    int fd;
    if (!source || !target || !stat(target,&status)) return;
    in=fopen(source,"rb"); if (!in) return;
    fd=open(target,O_WRONLY|O_CREAT|O_EXCL,0600);
    out=fd<0 ? NULL : fdopen(fd,"wb");
    if (fd>=0 && !out) close(fd);
    if (out) {
        int failed=0;
        while ((count=fread(bytes,1,sizeof(bytes),in)))
            if (fwrite(bytes,1,count,out)!=count) { failed=1; break; }
        if (ferror(in)) failed=1;
        if (fclose(out)) failed=1;
        if (failed) unlink(target);
    }
    fclose(in);
}
static void copy_tree(const char *source,const char *target,int depth)
{
    DIR *directory;
    struct dirent *entry;
    struct stat status;
    if(depth>8 || !source || !target || !zcmp(source,target))return;
    directory=opendir(source);if(!directory)return;
    mkdir(target,0700);
    while((entry=readdir(directory))!=NULL) {
        char *from,*to;
        if(!zcmp(entry->d_name,".") || !zcmp(entry->d_name,".."))continue;
        from=vsfmt(NULL,0,"%s/%s",source,entry->d_name);
        to=vsfmt(NULL,0,"%s/%s",target,entry->d_name);
        if(stat(from,&status))continue;
        if((status.st_mode&S_IFMT)==S_IFDIR) {
#ifndef JOEWIN
            struct stat link;
            if(!lstat(from,&link) && (link.st_mode&S_IFMT)==S_IFLNK)continue;
#endif
            copy_tree(from,to,depth+1);
        } else if((status.st_mode&S_IFMT)==S_IFREG)copy_missing(from,to);
    }
    closedir(directory);
}
void mj_profile_migrate(void)
{
    const char *home=getenv("HOME");
    char *target=mj_profile_path("joerc",1),*marker=mj_profile_path(".migrated",0);
    struct stat status;
    FILE *done;
    int fd;
    if (!home || !target || !marker || !stat(marker,&status)) return;
#ifndef JOEWIN
    copy_missing(vsfmt(NULL,0,"%s/.joerc",home),target);
    copy_missing(vsfmt(NULL,0,"%s/.joe_state",home),mj_profile_path("state",1));
    copy_tree(vsfmt(NULL,0,"%s/.joe",home),mj_profile_path("",1),0);
#else
    copy_missing(vsfmt(NULL,0,"%s/joerc",jw_home),target);
    copy_missing(vsfmt(NULL,0,"%s/joe_state",jw_home),mj_profile_path("state",1));
    copy_tree(jw_home,mj_profile_path("",1),0);
#endif
    {
        char *config=mj_profile_path("config",1);
        fd=config ? open(config,O_WRONLY|O_CREAT|O_EXCL,0600) : -1;
        done=fd<0 ? NULL : fdopen(fd,"wb");
        if(done) {
            fputs("# MiJoe personal global options (one option per line).\n"
                  "# Examples: uncomment the options you need.\n"
                  "# -softwrap\n# -rmargin 76\n# --wordwrap\n# -mouse\n# -colors teasp\n"
                  "# Advanced key bindings belong in joerc in this directory.\n",done);
            fclose(done);
        } else if(fd>=0)close(fd);
    }
    done=fopen(marker,"wb");if(done){fputs("MiJoe personal profile v1\n",done);fclose(done);}
}
char *mj_profile_asset(const char *group, const char *name, const char *extension)
{
    char *directory=mj_profile_path(group,1), *target, *old;
    const char *home=getenv("HOME");
    struct stat status;
    if (!directory) return NULL;
    target=vsfmt(NULL,0,"%s/%s%s",directory,name,extension);
    if (!stat(target,&status)) return target;
#ifndef JOEWIN
    old=home ? vsfmt(NULL,0,"%s/.joe/%s/%s%s",home,group,name,extension) : NULL;
#else
    old=jw_home ? vsfmt(NULL,0,"%s/%s/%s%s",jw_home,group,name,extension) : NULL;
#endif
    if (old && !stat(old,&status)) {
#ifdef JOEWIN
        mkdir(directory,0700);
#else
        mkdir(directory,0700);
#endif
        copy_missing(old,target);
    }
    return target;
}
int mj_profile_replace(const char *temporary, const char *target)
{
#ifdef JOEWIN
    int a=MultiByteToWideChar(CP_UTF8,0,temporary,-1,NULL,0);
    int b=MultiByteToWideChar(CP_UTF8,0,target,-1,NULL,0), result=-1;
    wchar_t *from=(wchar_t *)joe_malloc(a*sizeof(wchar_t));
    wchar_t *to=(wchar_t *)joe_malloc(b*sizeof(wchar_t));
    MultiByteToWideChar(CP_UTF8,0,temporary,-1,from,a);
    MultiByteToWideChar(CP_UTF8,0,target,-1,to,b);
    if (MoveFileExW(from,to,MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)) result=0;
    joe_free(from); joe_free(to); return result;
#else
    return rename(temporary,target);
#endif
}

/* The small config file is options-only, not a key-binding RC document. */
static int read_options(OPTIONS *options,int globals)
{
    char *path=mj_profile_path("config",0), *line=NULL;
    FILE *f=path ? fopen(path,"rb") : NULL;
    int changed=0;
    if(!f)return 0;
    while(vsgets(&line,f)) {
        char *name=line,*end,*arg=NULL;
        while(*name==' ' || *name=='\t')++name;
        if(name[0]!='-')continue;
        end=name+1;while(*end && *end!=' ' && *end!='\t')++end;
        if(*end){*end++=0;while(*end==' ' || *end=='\t')++end;arg=end;}
        if(glopt(name+1,arg,options,globals))changed=1;
        else if(globals)logerror_1("Unknown option in ~/.mj/config: %s\n",name+1);
    }
    fclose(f);return changed;
}
int mj_profile_local_options(OPTIONS *options) { return read_options(options,0); }
void mj_profile_configure(void) { read_options(&fdefault,1); }
