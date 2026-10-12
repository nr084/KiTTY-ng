/* Private anonymous stdin pipe supplied by the bundled GUI parent. */
#include "putty.h"
#include "helper.h"
#include <unistd.h>
bool kr_mac_import_conf(Conf *conf,const char *source){
    if(strcmp(source,"stdin")||!kr_mac_gui_prompts())return false;
    setpgid(0,0);
    unsigned char header[12];if(fread(header,1,sizeof(header),stdin)!=sizeof(header)||memcmp(header,"KNGMAC1\n",8))return false;
    unsigned length=((unsigned)header[8]<<24)|((unsigned)header[9]<<16)|((unsigned)header[10]<<8)|header[11];
    if(!length||length>2097152)return false;unsigned char *data=snewn(length,unsigned char);bool valid=fread(data,1,length,stdin)==length;
    /* Keep defaults for omitted fields; never leave a partial Conf with missing scalars. */
    Conf *incoming=conf_copy(conf);BinarySource reader[1];BinarySource_BARE_INIT(reader,data,length);
    valid=valid&&conf_deserialise(incoming,reader)&&!get_err(reader)&&!get_avail(reader);
    if(valid)conf_copy_into(conf,incoming);conf_free(incoming);smemclr(data,length);sfree(data);return valid;
}
