/* Portable UTF-8/CP1251 name decoding; no operating-system dependencies. */
#ifndef KITTY_NG_NAMES_H
#define KITTY_NG_NAMES_H
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
static bool kr_name_valid_utf8(const unsigned char *s,size_t length){for(size_t i=0;i<length;){unsigned c=s[i++],n,min;if(c<128)continue;if(c>=194&&c<=223){n=1;min=128;c&=31;}else if(c>=224&&c<=239){n=2;min=2048;c&=15;}else if(c>=240&&c<=244){n=3;min=65536;c&=7;}else return false;if(i+n>length)return false;while(n--){unsigned b=s[i++];if((b&192)!=128)return false;c=(c<<6)|(b&63);}if(c<min||c>1114111||(c>=55296&&c<=57343))return false;}return true;}
static char *kr_name_from_bytes(const char *raw){size_t n=strlen(raw);if(kr_name_valid_utf8((const unsigned char*)raw,n)){char *out=malloc(n+1);if(out)memcpy(out,raw,n+1);return out;}
    static const unsigned cp1251[128]={1026,1027,8218,1107,8222,8230,8224,8225,8364,8240,1033,8249,1034,1036,1035,1039,1106,8216,8217,8220,8221,8226,8211,8212,0,8482,1113,8250,1114,1116,1115,1119,160,1038,1118,1032,164,1168,166,167,1025,169,1028,171,172,173,174,1031,176,177,1030,1110,1169,181,182,183,1105,8470,1108,187,1112,1029,1109,1111,1040,1041,1042,1043,1044,1045,1046,1047,1048,1049,1050,1051,1052,1053,1054,1055,1056,1057,1058,1059,1060,1061,1062,1063,1064,1065,1066,1067,1068,1069,1070,1071,1072,1073,1074,1075,1076,1077,1078,1079,1080,1081,1082,1083,1084,1085,1086,1087,1088,1089,1090,1091,1092,1093,1094,1095,1096,1097,1098,1099,1100,1101,1102,1103};char *out=malloc(n*3+1);if(!out)return NULL;size_t k=0;for(size_t i=0;i<n;i++){unsigned c=(unsigned char)raw[i];if(c>=128)c=cp1251[c-128];if(!c){free(out);return NULL;}if(c<128)out[k++]=c;else if(c<2048){out[k++]=192|(c>>6);out[k++]=128|(c&63);}else{out[k++]=224|(c>>12);out[k++]=128|((c>>6)&63);out[k++]=128|(c&63);}}out[k]=0;return out;
}

static char *kr_text_from_bytes(const unsigned char *bytes,size_t length){
    if(length>=2&&((bytes[0]==255&&bytes[1]==254)||(bytes[0]==254&&bytes[1]==255))){if(length%2)return NULL;bool little=bytes[0]==255;char *out=malloc(length*2+1);if(!out)return NULL;size_t n=0;for(size_t i=2;i<length;i+=2){unsigned c=little?bytes[i]|((unsigned)bytes[i+1]<<8):((unsigned)bytes[i]<<8)|bytes[i+1];if(!c){free(out);return NULL;}if(c>=0xd800&&c<=0xdbff){if(i+3>=length){free(out);return NULL;}unsigned next=little?bytes[i+2]|((unsigned)bytes[i+3]<<8):((unsigned)bytes[i+2]<<8)|bytes[i+3];if(next<0xdc00||next>0xdfff){free(out);return NULL;}c=0x10000+((c-0xd800)<<10)+(next-0xdc00);i+=2;}else if(c>=0xdc00&&c<=0xdfff){free(out);return NULL;}if(c<128)out[n++]=c;else if(c<2048){out[n++]=192|(c>>6);out[n++]=128|(c&63);}else if(c<65536){out[n++]=224|(c>>12);out[n++]=128|((c>>6)&63);out[n++]=128|(c&63);}else{out[n++]=240|(c>>18);out[n++]=128|((c>>12)&63);out[n++]=128|((c>>6)&63);out[n++]=128|(c&63);}}out[n]=0;return out;}
    if(length>=3&&bytes[0]==239&&bytes[1]==187&&bytes[2]==191){bytes+=3;length-=3;}if(memchr(bytes,0,length))return NULL;char *raw=malloc(length+1);if(!raw)return NULL;memcpy(raw,bytes,length);raw[length]=0;char *out=kr_name_from_bytes(raw);free(raw);return out;
}
#endif
