/* External WinSCP integration, Windows only. Never export saved passwords. */
#include "putty.h"
#include <wchar.h>
#include <shellapi.h>
#include "kittyng-portable.h"
#include "kittyng-extensions.h"
#include "kittyng-companions.h"
#include "kittyng-winscp.h"

static wchar_t *winscp_root(void)
{
    if (kr_portable_root()) return dupwcs(kr_portable_root());
    wchar_t module[32768];
    DWORD n = GetModuleFileNameW(NULL, module, lenof(module));
    if (!n || n >= lenof(module)) return NULL;
    wchar_t *slash = wcsrchr(module, L'\\');
    if (!slash) return NULL;
    *slash = 0; return dupwcs(module);
}

static wchar_t *resolve_path(const char *setting)
{
    wchar_t *wide = dup_mb_to_wc(CP_UTF8, setting);
    DWORD n = ExpandEnvironmentStringsW(wide, NULL, 0);
    wchar_t *expanded = n ? snewn(n, wchar_t) : NULL;
    if (!expanded || !ExpandEnvironmentStringsW(wide, expanded, n)) {
        sfree(wide); sfree(expanded); return NULL;
    }
    sfree(wide);
    if (!*expanded) { sfree(expanded); return NULL; }
    if (*expanded != L'/' && *expanded != L'\\' && expanded[1] != L':') {
        wchar_t *root = winscp_root();
        if (!root) { sfree(expanded); return NULL; }
        wchar_t *joined = snewn(wcslen(root)+wcslen(expanded)+2, wchar_t);
        wcscpy(joined, root); wcscat(joined, L"\\"); wcscat(joined, expanded);
        sfree(root); sfree(expanded); expanded = joined;
    }
    for (wchar_t *p=expanded; *p; p++) if (*p==L'/') *p=L'\\';
    return expanded;
}

static bool executable(const wchar_t *path, DWORD *error)
{
    DWORD attr = GetFileAttributesW(path);
    if (attr == INVALID_FILE_ATTRIBUTES) { *error = GetLastError(); return false; }
    if (attr & FILE_ATTRIBUTE_DIRECTORY) { *error = ERROR_DIRECTORY; return false; }
    size_t n = wcslen(path);
    if (n < 4 || _wcsicmp(path+n-4, L".exe")) {
        *error = ERROR_NOT_SUPPORTED; return false;
    }
    *error = ERROR_SUCCESS; return true;
}

wchar_t *kr_winscp_path(DWORD *error)
{
    char *setting = kr_ini_string("KiTTY", "WinSCPPath", "");
    if (*setting) {
        wchar_t *path = resolve_path(setting); sfree(setting);
        if (path && executable(path, error)) return path;
        if (!path) *error = ERROR_INVALID_NAME;
        sfree(path); return NULL; /* Preserve an explicit, temporarily unavailable path. */
    }
    sfree(setting);
    const char *locations[] = {"%ProgramFiles%\\WinSCP\\WinSCP.exe",
        "%ProgramFiles(x86)%\\WinSCP\\WinSCP.exe",
        "%ProgramW6432%\\WinSCP\\WinSCP.exe", "WinSCP.exe",
        "%ProgramFiles%\\WinSCP3\\WinSCP3.exe"};
    for (size_t i=0; i<lenof(locations); i++) {
        wchar_t *path = resolve_path(locations[i]);
        if (path && executable(path, error)) return path;
        sfree(path);
    }
    setting = kr_ini_string("KiTTY", "winscpdir", "");
    if (*setting) {
        char *joined = dupprintf("%s\\WinSCP.exe", setting);
        wchar_t *path = resolve_path(joined); sfree(joined); sfree(setting);
        if (path && executable(path, error)) return path;
        sfree(path);
    } else sfree(setting);
    *error = ERROR_FILE_NOT_FOUND; return NULL;
}

static void argument(strbuf *args, const char *value)
{
    if (args->len) put_byte(args, ' ');
    kr_quote_companion_argument(args, value);
}
static void encode_path(strbuf *url, const char *s)
{
    for (; *s; s++) {
        unsigned char c = *s;
        if ((c>='a' && c<='z') || (c>='A' && c<='Z') ||
            (c>='0' && c<='9') || strchr("/-._~", c)) put_byte(url,c);
        else put_fmt(url,"%%%02X",c);
    }
}
static void options(strbuf *args, const char *value)
{
    wchar_t *wide = dup_mb_to_wc(CP_UTF8,value);
    wchar_t *line = snewn(wcslen(wide)+8,wchar_t);
    wcscpy(line,L"winscp "); wcscat(line,wide); sfree(wide);
    int count; wchar_t **argv = CommandLineToArgvW(line,&count); sfree(line);
    if (!argv) return;
    for (int i=1;i<count;i++) {
        char *text = kr_transfer_to_utf8(argv[i]);
        argument(args,text); sfree(text);
    }
    LocalFree(argv);
}

wchar_t *kr_winscp_arguments(Conf *conf, DWORD *error)
{
    *error = ERROR_INVALID_PARAMETER;
    if (conf_get_int(conf,CONF_protocol) != PROT_SSH) {
        *error = ERROR_NOT_SUPPORTED; return NULL;
    }
    bool user_utf8;
    const char *username = conf_get_str_ambi(conf,CONF_username,&user_utf8);
    char *host = dupstr(conf_get_str(conf,CONF_host));
    wchar_t *user_wide=dup_mb_to_wc(user_utf8 ? CP_UTF8 : CP_ACP,username);
    char *user = kr_transfer_to_utf8(user_wide); sfree(user_wide);
    int port = conf_get_int(conf,CONF_port);
    const char *alternate = conf_get_str(conf,CONF_sftpconnect);
    if (*alternate) {
        sfree(host); host=dupstr(alternate);
        char *at=strrchr(host,'@');
        if (at) { *at=0; sfree(user); user=dupstr(host); memmove(host,at+1,strlen(at+1)+1); }
        char *colon=NULL;
        if (*host=='[') {
            char *end=strchr(host,']');
            if (!end || (end[1] && end[1]!=':')) goto invalid;
            if (end[1]==':') colon=end+1;
        } else {
            colon=strchr(host,':');
            if (colon && strchr(colon+1,':')) colon=NULL;
        }
        if (colon) {
            char *end; long number=strtol(colon+1,&end,10);
            if (!colon[1] || *end || number<1 || number>65535) goto invalid;
            port=number; *colon=0;
        }
    }
    if (!*host || port<1 || port>65535 || strpbrk(host," /\\\r\n\"@?#%")) goto invalid;
    strbuf *url=strbuf_new(), *args=strbuf_new();
    put_fmt(url,"%s://",conf_get_int(conf,CONF_winscpprot)==0 ? "scp" : "sftp");
    bool ipv6=strchr(host,':') && host[0]!='[';
    put_fmt(url,"%s%s%s:%d/",ipv6?"[":"",host,ipv6?"]":"",port);
    const char *dir=conf_get_str(conf,CONF_pscpremotedir);
    if (*dir) {
        if (*dir=='/') dir++;
        encode_path(url,dir);
        if (url->s[url->len-1]!='/') put_byte(url,'/');
    }
    argument(args,url->s); strbuf_free(url);
    if (*user) {
        char *value=dupprintf("/username=%s",user); argument(args,value); sfree(value);
    }
    const Filename *key=conf_get_filename(conf,CONF_keyfile);
    if (!filename_is_null(key)) {
        char *utf8=kr_transfer_to_utf8(key->wpath);
        char *value=dupprintf("/privatekey=%s",utf8);
        argument(args,value); sfree(value); sfree(utf8);
    }
    options(args,conf_get_str(conf,CONF_winscpoptions));
    const char *raw=conf_get_str(conf,CONF_winscprawsettings);
    if (*raw || conf_get_bool(conf,CONF_compression) || conf_get_bool(conf,CONF_agentfwd)) {
        argument(args,"/rawsettings");
        if (conf_get_bool(conf,CONF_compression)) argument(args,"Compression=1");
        if (conf_get_bool(conf,CONF_agentfwd)) argument(args,"AgentFwd=1");
        options(args,raw);
    }
    wchar_t *result=dup_mb_to_wc(CP_UTF8,args->s);
    strbuf_free(args); sfree(host); sfree(user); *error=ERROR_SUCCESS; return result;
  invalid:
    sfree(host); sfree(user); return NULL;
}

bool kr_launch_winscp(Conf *conf, DWORD *error)
{
    wchar_t *path=kr_winscp_path(error);
    if (!path) return false;
    wchar_t *args=kr_winscp_arguments(conf,error);
    if (!args) { sfree(path); return false; }
    size_t n=wcslen(path)+wcslen(args)+4;
    wchar_t *command=snewn(n,wchar_t);
    swprintf(command,n,L"\"%ls\" %ls",path,args);
    STARTUPINFOW startup={0}; PROCESS_INFORMATION process={0}; startup.cb=sizeof(startup);
    wchar_t *root=winscp_root();
    bool ok=CreateProcessW(path,command,NULL,NULL,false,0,NULL,root,&startup,&process);
    *error=ok ? ERROR_SUCCESS : GetLastError();
    if (ok) { CloseHandle(process.hThread); CloseHandle(process.hProcess); }
    sfree(root); sfree(command); sfree(args); sfree(path); return ok;
}
