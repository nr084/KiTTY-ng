/* Native-only integration checks in the smoke script's disposable root. */
#include "putty.h"
#include "storage.h"
#include "portable.h"
#include <unistd.h>
#include <stdlib.h>
int kr_mac_self_test(const char *public_file){
    const char *enabled=getenv("KITTY_NG_SELF_TEST"),*root=kr_mac_root();if(!enabled||strcmp(enabled,"1")||!root||(!strstr(root,"/kitty-ng-smoke."))||(strncmp(root,"/tmp/",5)&&strncmp(root,"/private/tmp/",13)))return 2;
    int status=1;Conf *conf=conf_new(),*loaded=conf_new();settings_r *reader=NULL;host_ca *ca=NULL,*again=NULL;char *failure=NULL,*path=NULL,*legacy=NULL;strbuf *text=NULL;FILE *fp=NULL;
#define CHECK(c,message) do{if(!(c)){fprintf(stderr,"FAIL: %s\n",message);goto finish;}}while(0)
    load_settings("Default Settings",conf);conf_set_str(conf,CONF_host,"127.0.0.1");conf_set_str(conf,CONF_username,"synthetic-smoke-user");char *keypath=dupcat(root,"/key.ppk");Filename *key=filename_from_str(keypath);sfree(keypath);conf_set_filename(conf,CONF_keyfile,key);filename_free(key);
    conf_set_int_int(conf,CONF_colours,0,17);conf_set_int_int(conf,CONF_colours,1,34);conf_set_int_int(conf,CONF_colours,2,51);conf_set_str_str(conf,CONF_portfwd,"L8080","example.invalid:80");
    reader=kr_mac_snapshot_settings(conf,save_open_settings);char *value=read_setting_s(reader,"Colour0");CHECK(value&&!strcmp(value,"17,34,51"),"colour serializer");sfree(value);value=read_setting_s(reader,"PortForwardings");CHECK(value&&strstr(value,"L8080"),"tunnel serializer");sfree(value);close_settings_r(reader);reader=NULL;
    failure=save_settings("Профили",conf);CHECK(!failure&&kr_mac_write_succeeded(),"profile save");reader=open_settings_r("Профили");value=read_setting_s(reader,"PublicKeyFile");CHECK(value&&!strcmp(value,"key.ppk"),"relative private key");sfree(value);close_settings_r(reader);reader=NULL;
    path=kr_mac_profile_path("Профили");CHECK(path,"profile filename");char *slash=strrchr(path,'/');CHECK(slash,"profile parent");char *parent=snewn(slash-path+1,char);memcpy(parent,path,slash-path);parent[slash-path]=0;
    legacy=dupcat(parent,"/%CF%F0%EE%F4%E8%EB%E8");sfree(parent);CHECK(!rename(path,legacy),"legacy filename setup");CHECK(load_settings("Профили",loaded),"CP1251 profile load");failure=save_settings("Профили",loaded);CHECK(!failure&&kr_mac_write_succeeded()&&!access(legacy,F_OK),"legacy filename preserved on save");
    fp=fopen(public_file,"rb");CHECK(fp,"synthetic public key");char *line=fgetline(fp);fclose(fp);fp=NULL;CHECK(line,"public key line");char *space=strchr(line,' ');CHECK(space,"public key format");char *blob=space+1;blob[strcspn(blob," \r\n")]=0;CHECK(base64_valid(ptrlen_from_asciz(blob)),"public key base64");ca=host_ca_new();ca->name=dupstr("synthetic-smoke-ca");ca->ca_public_key=base64_decode_sb(ptrlen_from_asciz(blob));ca->validity_expression=dupstr("*.example.invalid");sfree(line);failure=host_ca_save(ca);CHECK(!failure,"CA save/validation");again=host_ca_load(ca->name);CHECK(again&&ptrlen_eq_ptrlen(ptrlen_from_strbuf(ca->ca_public_key),ptrlen_from_strbuf(again->ca_public_key))&&!strcmp(ca->validity_expression,again->validity_expression)&&!again->opts.permit_rsa_sha1&&again->opts.permit_rsa_sha256&&again->opts.permit_rsa_sha512,"CA round trip");failure=host_ca_delete(ca->name);CHECK(!failure,"CA deletion");del_settings("Профили");puts("PASS: native profile/CP1251/relative-key/custom-settings/SSH-CA integration");status=0;
finish:
    if(fp)fclose(fp);close_settings_r(reader);if(ca)host_ca_free(ca);if(again)host_ca_free(again);conf_free(conf);conf_free(loaded);sfree(failure);sfree(path);sfree(legacy);if(text)strbuf_free(text);return status;
#undef CHECK
}
