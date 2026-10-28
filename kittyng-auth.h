/* Runtime-only authentication for independent duplicate sessions. */
#ifndef KITTYNG_AUTH_H
#define KITTYNG_AUTH_H
#include "putty.h"
static inline char *kr_auth_identity(Conf *conf)
{
    return dupprintf("%d:%d:%s\n%s\n%s", conf_get_int(conf, CONF_protocol),
        conf_get_int(conf, CONF_port), conf_get_str(conf, CONF_host),
        conf_get_str_ambi(conf, CONF_username, NULL),
        filename_to_str(conf_get_filename(conf, CONF_keyfile)));
}
static inline bool kr_auth_matches(Conf *conf)
{
    char *id=kr_auth_identity(conf);
    bool match=!strcmp(id,conf_get_str(conf,CONF_kr_auth_identity));
    sfree(id);return match;
}
static inline int kr_auth_prompt(prompts_t *p)
{
    if(p->n_prompts!=1 || p->from_server || !p->name)return -1;
    if(p->to_server && p->prompts[0]->echo && !strcmp(p->name,"SSH login name"))return CONF_kr_auth_user;
    if(p->to_server && !p->prompts[0]->echo && !strcmp(p->name,"SSH password"))return CONF_kr_auth_password;
    if(!p->to_server && !p->prompts[0]->echo && !strcmp(p->name,"SSH key passphrase"))return CONF_kr_auth_passphrase;
    return -1; /* Never cache OTPs, server challenges or password changes. */
}
static inline bool kr_auth_reuse(Conf *conf, prompts_t *p)
{
    int key=kr_auth_prompt(p);
    if(key<0 || !kr_auth_matches(conf))return false;
    int bit=1<<(key-CONF_kr_auth_user),used=conf_get_int(conf,CONF_kr_auth_attempts);
    const char *value=conf_get_str(conf,key);
    if((used&bit) || !*value)return false;
    conf_set_int(conf,CONF_kr_auth_attempts,used|bit);
    prompt_set_result(p->prompts[0],value);return true;
}
static inline void kr_auth_capture(Conf *conf, prompts_t *p, SeatPromptResult result)
{
    int key=kr_auth_prompt(p);
    if(key<0 || result.kind!=SPRK_OK)return;
    if(!kr_auth_matches(conf)){
        conf_set_str(conf,CONF_kr_auth_user,"");
        conf_set_str(conf,CONF_kr_auth_password,"");
        conf_set_str(conf,CONF_kr_auth_passphrase,"");
    }
    char *id=kr_auth_identity(conf);conf_set_str(conf,CONF_kr_auth_identity,id);sfree(id);
    conf_set_str(conf,key,prompt_get_result_ref(p->prompts[0]));
    conf_set_int(conf,CONF_kr_auth_attempts,conf_get_int(conf,CONF_kr_auth_attempts)|(1<<(key-CONF_kr_auth_user)));
}
static inline Conf *kr_auth_duplicate(Conf *source, bool authenticated)
{
    Conf *copy=conf_copy(source);
    conf_set_int(copy,CONF_kr_auth_attempts,0);
    if(!authenticated || !kr_auth_matches(source)){
        conf_set_str(copy,CONF_kr_auth_user,"");
        conf_set_str(copy,CONF_kr_auth_password,"");
        conf_set_str(copy,CONF_kr_auth_passphrase,"");
        conf_set_str(copy,CONF_kr_auth_identity,"");
    }
    return copy;
}
#endif
