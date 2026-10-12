/* Bundled Universal kscp with a private Conf pipe and native SSH prompts. */
#import <AppKit/AppKit.h>
#import <Security/Security.h>
#include "putty.h"
#include "portable.h"
#include "transfer.h"
#include "transfer-path.h"
#include "language.h"
#include <unistd.h>
#include <signal.h>
#include <sys/stat.h>
static NSMutableArray *jobs;
static NSString *text(const char *s){return [NSString stringWithUTF8String:s]?:@"";}
static NSString *tr(NSString *en,NSString *ru){return kr_mac_language()==1?ru:en;}
static void error(NSString *s){NSAlert *a=[NSAlert new];a.messageText=@"KiTTY-ng";a.informativeText=s;[a runModal];}
static NSURL *helper(NSString *name){
    if(![@[@"klink",@"kscp",@"ksftp",@"kittygen",@"kageant"]containsObject:name])return nil;
    NSURL *bundle=NSBundle.mainBundle.bundleURL,*path=[bundle URLByAppendingPathComponent:[@"Contents/Helpers/" stringByAppendingString:name]];
    struct stat st;if(lstat(path.path.fileSystemRepresentation,&st)||!S_ISREG(st.st_mode)||access(path.path.fileSystemRepresentation,X_OK))return nil;
    NSData *header=[NSData dataWithContentsOfURL:path];if(header.length<48)return nil;
    const unsigned char *p=header.bytes;if(p[0]!=0xca||p[1]!=0xfe||p[2]!=0xba||p[3]!=0xbe||p[4]||p[5]||p[6]||p[7]!=2)return nil;
    unsigned cpus[2];for(int i=0;i<2;i++){const unsigned char *c=p+8+20*i;cpus[i]=((unsigned)c[0]<<24)|((unsigned)c[1]<<16)|((unsigned)c[2]<<8)|c[3];}
    if(!((cpus[0]==0x1000007&&cpus[1]==0x100000c)||(cpus[1]==0x1000007&&cpus[0]==0x100000c)))return nil;
    SecStaticCodeRef code=NULL;OSStatus status=SecStaticCodeCreateWithPath((__bridge CFURLRef)bundle,kSecCSDefaultFlags,&code);
    if(!status)status=SecStaticCodeCheckValidity(code,kSecCSCheckAllArchitectures|kSecCSCheckNestedCode|kSecCSStrictValidate,NULL);if(code)CFRelease(code);
    return status==errSecSuccess?path:nil;
}
@interface MacTransfer : NSObject <NSWindowDelegate>
@property(nonatomic,strong) NSWindow *window;
@property(nonatomic,strong) NSTask *task;
@property(nonatomic,strong) NSTextView *output;
@property(nonatomic,strong) NSMutableData *transcript;
@property(nonatomic,strong) NSButton *cancel;
@property(nonatomic,strong) NSProgressIndicator *progress;
@property(nonatomic,strong) NSPipe *pipe;
@end
@implementation MacTransfer
- (void)cancelJob:(id)sender {if(self.task.running){kill(-self.task.processIdentifier,SIGTERM);[self.task terminate];}else[self.window close];}
- (BOOL)windowShouldClose:(NSWindow *)window {if(self.task.running){[self cancelJob:nil];return NO;}return YES;}
- (void)windowWillClose:(NSNotification *)note {self.window.delegate=nil;self.window=nil;[jobs removeObject:self];}
- (void)append:(NSData *)data {
    if(self.transcript.length+data.length>2097152)[self.transcript setLength:0];[self.transcript appendData:data];
    NSString *value=[[NSString alloc]initWithData:self.transcript encoding:NSUTF8StringEncoding];if(value){self.output.string=value;[self.output scrollRangeToVisible:NSMakeRange(value.length,0)];}
}
@end
static NSTask *startJob(NSURL *program,NSArray *args,NSMutableData *snapshot,NSString *title,void(^completion)(int,NSString*)){
    if(!jobs){jobs=[NSMutableArray new];[NSNotificationCenter.defaultCenter addObserverForName:NSApplicationWillTerminateNotification object:nil queue:nil usingBlock:^(NSNotification *n){for(MacTransfer *job in jobs)if(job.task.running){kill(-job.task.processIdentifier,SIGTERM);[job.task terminate];}}];}
    MacTransfer *job=[MacTransfer new];job.transcript=[NSMutableData new];job.task=[NSTask new];job.task.executableURL=program;job.task.arguments=args;
    NSMutableDictionary *env=[NSProcessInfo.processInfo.environment mutableCopy];env[@"KITTY_NG_GUI_PROMPTS"]=@"1";env[@"KITTY_NG_LANGUAGE"]=kr_mac_language()==1?@"ru":@"en";env[@"KITTY_NG_PORTABLE_DIR"]=text(kr_mac_root());const char *socket=getenv("SSH_AUTH_SOCK");if(socket)env[@"SSH_AUTH_SOCK"]=text(socket);else [env removeObjectForKey:@"SSH_AUTH_SOCK"];job.task.environment=env;
    NSPipe *input=[NSPipe pipe];job.pipe=[NSPipe pipe];job.task.standardInput=snapshot?(id)input:NSFileHandle.fileHandleWithNullDevice;job.task.standardOutput=job.pipe;job.task.standardError=job.pipe;
    job.window=[[NSWindow alloc]initWithContentRect:NSMakeRect(0,0,700,420) styleMask:NSWindowStyleMaskTitled|NSWindowStyleMaskClosable|NSWindowStyleMaskResizable backing:NSBackingStoreBuffered defer:NO];job.window.releasedWhenClosed=NO;job.window.title=title;job.window.delegate=job;
    NSScrollView *scroll=[[NSScrollView alloc]initWithFrame:NSMakeRect(16,65,668,339)];scroll.hasVerticalScroller=YES;scroll.autoresizingMask=NSViewWidthSizable|NSViewHeightSizable;
    job.output=[[NSTextView alloc]initWithFrame:scroll.bounds];job.output.editable=NO;job.output.font=[NSFont monospacedSystemFontOfSize:12 weight:NSFontWeightRegular];scroll.documentView=job.output;[job.window.contentView addSubview:scroll];
    job.progress=[[NSProgressIndicator alloc]initWithFrame:NSMakeRect(16,25,500,16)];job.progress.indeterminate=YES;job.progress.style=NSProgressIndicatorStyleBar;job.progress.autoresizingMask=NSViewWidthSizable;[job.window.contentView addSubview:job.progress];[job.progress startAnimation:nil];
    job.cancel=[NSButton buttonWithTitle:tr(@"Cancel",@"Отмена") target:job action:@selector(cancelJob:)];job.cancel.frame=NSMakeRect(550,16,130,32);job.cancel.autoresizingMask=NSViewMinXMargin;[job.window.contentView addSubview:job.cancel];[jobs addObject:job];
    __weak MacTransfer *weak=job;
    NSError *failure=nil;if(![job.task launchAndReturnError:&failure]){[jobs removeObject:job];[snapshot resetBytesInRange:NSMakeRange(0,snapshot.length)];error(failure.localizedDescription);return nil;}
    NSFileHandle *reader=job.pipe.fileHandleForReading;
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_UTILITY,0),^{@autoreleasepool{
        @try{for(;;){@autoreleasepool{NSData *data=reader.availableData;if(!data.length)break;dispatch_async(dispatch_get_main_queue(),^{MacTransfer *j=weak;if(j)[j append:data];});}}}@catch(NSException *e){}
        [reader closeFile];[job.task waitUntilExit];int status=job.task.terminationStatus;
        dispatch_async(dispatch_get_main_queue(),^{MacTransfer *j=weak;if(j){[j.progress stopAnimation:nil];j.cancel.title=tr(@"Close",@"Закрыть");j.window.title=[title stringByAppendingString:status==0?tr(@" — completed",@" — завершено"):tr(@" — failed",@" — ошибка")];}if(completion)completion(status,j?([[NSString alloc]initWithData:j.transcript encoding:NSUTF8StringEncoding]?:@""):@"");});
    }});
    if(snapshot)dispatch_async(dispatch_get_global_queue(QOS_CLASS_UTILITY,0),^{@autoreleasepool{@try{[input.fileHandleForWriting writeData:snapshot];}@catch(NSException *e){}[input.fileHandleForWriting closeFile];[snapshot resetBytesInRange:NSMakeRange(0,snapshot.length)];}});
    [job.window center];[job.window makeKeyAndOrderFront:nil];return job.task;
}
NSTask *kr_mac_run_helper(NSString *name,NSArray<NSString*> *args,NSString *title,void(^completion)(int,NSString*)){
    NSURL *program=helper(name);if(!program){error(tr(@"Bundled helper is missing or its signature is invalid",@"Встроенная утилита отсутствует либо её подпись повреждена"));return nil;}
    return startJob(program,args,nil,title,completion);
}
static BOOL destination(Conf *conf){
    const char *alternate=conf_get_str(conf,CONF_sftpconnect);
    if(*alternate){char *copy=dupstr(alternate),*host=copy,*at=strrchr(copy,'@'),*port=NULL;if(at){*at=0;conf_set_str(conf,CONF_username,copy);host=at+1;}
        if(*host=='['){char *close=strchr(host,']');if(!close){sfree(copy);return NO;}if(close[1]==':')port=close+2;else if(close[1]){sfree(copy);return NO;}*close=0;host++;}
        else{char *colon=strchr(host,':');if(colon&&!strchr(colon+1,':')){*colon=0;port=colon+1;}}
        if(!*host){sfree(copy);return NO;}if(port){char *end;long n=strtol(port,&end,10);if(!*port||*end||n<1||n>65535){sfree(copy);return NO;}conf_set_int(conf,CONF_port,n);}conf_set_str(conf,CONF_host,host);sfree(copy);
    }
    char *port=kr_mac_ini("KiTTY","pscpport","*");if(*port&&strcmp(port,"*")){char *end;long n=strtol(port,&end,10);if(*end||n<1||n>65535){sfree(port);return NO;}conf_set_int(conf,CONF_port,n);}sfree(port);return *conf_get_str(conf,CONF_host)!=0;
}
static void launch(Conf *original,BOOL upload,NSArray<NSURL *> *files,NSString *remote){
    if(conf_get_int(original,CONF_protocol)!=PROT_SSH){error(tr(@"File transfer requires SSH",@"Для передачи файлов нужен SSH"));return;}
    NSURL *program=helper(@"kscp");if(!program){error(tr(@"Bundled Universal kscp is missing or its signature is invalid",@"Встроенный Universal kscp отсутствует либо его подпись повреждена"));return;}
    Conf *conf=conf_copy(original);if(!destination(conf)){conf_free(conf);error(tr(@"Invalid SFTPConnect or pscpport setting",@"Некорректная настройка SFTPConnect или pscpport"));return;}
    NSMutableArray *args=[NSMutableArray arrayWithObjects:@"--kitty-ng-conf",@"stdin",nil];
    NSSet *allowed=[NSSet setWithArray:@[@"-r",@"-p",@"-q",@"-v",@"-scp",@"-sftp",@"-batch"]];
    for(NSString *option in [text(conf_get_str(conf,CONF_pscpoptions)) componentsSeparatedByCharactersInSet:NSCharacterSet.whitespaceAndNewlineCharacterSet]){
        if(!option.length)continue;if(![allowed containsObject:option]){conf_free(conf);error([tr(@"Unsupported PSCPOptions argument: ",@"Неподдерживаемый аргумент PSCPOptions: ") stringByAppendingString:option]);return;}[args addObject:option];}
    [args addObject:conf_get_int(conf,CONF_winscpprot)==0?@"-scp":@"-sftp"];[args addObject:@"--"];
    NSString *host=text(conf_get_str(conf,CONF_host));if([host containsString:@":"]&&![host hasPrefix:@"["])host=[NSString stringWithFormat:@"[%@]",host];
    NSString *target=[NSString stringWithFormat:@"%@:%@",host,remote.length?remote:@"."];
    if(!files.count){conf_free(conf);error(tr(@"No local files selected",@"Локальные файлы не выбраны"));return;}
    if(upload){for(NSURL *file in files){NSNumber *directory=nil;[file getResourceValue:&directory forKey:NSURLIsDirectoryKey error:nil];if(directory.boolValue){if(![args containsObject:@"-r"])[args insertObject:@"-r" atIndex:2];break;}}}
    if(!upload)[args addObject:target];for(NSURL *file in files){if(!file.isFileURL||!file.path.length){conf_free(conf);error(tr(@"Invalid local file path",@"Некорректный путь локального файла"));return;}[args addObject:file.path];}if(upload)[args addObject:target];
    strbuf *serialized=strbuf_new();conf_serialise(BinarySink_UPCAST(serialized),conf);conf_free(conf);
    if(serialized->len>2097152){smemclr(serialized->s,serialized->len);strbuf_free(serialized);error(@"Session configuration is too large");return;}
    unsigned char header[12]={'K','N','G','M','A','C','1','\n',serialized->len>>24,serialized->len>>16,serialized->len>>8,serialized->len};
    NSMutableData *snapshot=[NSMutableData dataWithBytes:header length:sizeof(header)];[snapshot appendBytes:serialized->s length:serialized->len];smemclr(serialized->s,serialized->len);strbuf_free(serialized);
    startJob(program,args,snapshot,tr(@"KiTTY-ng — file transfer",@"KiTTY-ng — передача файлов"),nil);
}
static NSString *askRemote(Conf *conf,BOOL upload,NSString *directory){
    NSAlert *a=[NSAlert new];a.messageText=tr(@"File transfer",@"Передача файлов");a.informativeText=upload?tr(@"Remote destination directory",@"Каталог назначения на сервере"):tr(@"Remote file or directory to download",@"Удалённый файл или каталог для скачивания");
    NSTextField *field=[[NSTextField alloc]initWithFrame:NSMakeRect(0,0,420,28)];field.stringValue=directory.length?directory:text(conf_get_str(conf,CONF_pscpremotedir));if(!field.stringValue.length)field.stringValue=@".";a.accessoryView=field;
    [a addButtonWithTitle:tr(@"Continue",@"Продолжить")];[a addButtonWithTitle:tr(@"Cancel",@"Отмена")];[a.window makeFirstResponder:field];return [a runModal]==NSAlertFirstButtonReturn?field.stringValue:nil;
}
void kr_mac_upload(Conf *source,NSWindow *owner,NSArray<NSURL *> *files,NSString *directory){
    Conf *conf=conf_copy(source);
    if(!files){NSOpenPanel *panel=[NSOpenPanel openPanel];panel.canChooseFiles=YES;panel.canChooseDirectories=YES;panel.allowsMultipleSelection=YES;if([panel runModal]!=NSModalResponseOK){conf_free(conf);return;}files=panel.URLs;}
    if(files.count){NSString *remote=text(kr_mac_upload_path(directory.UTF8String,conf_get_str(conf,CONF_pscpremotedir)));launch(conf,YES,files,remote);}conf_free(conf);
}
void kr_mac_download(Conf *source,NSWindow *owner,NSString *directory){
    Conf *conf=conf_copy(source);NSString *remote=askRemote(conf,NO,directory);if(remote.length){NSOpenPanel *panel=[NSOpenPanel openPanel];panel.canChooseFiles=NO;panel.canChooseDirectories=YES;if([panel runModal]==NSModalResponseOK)launch(conf,NO,@[panel.URL],remote);}conf_free(conf);
}
