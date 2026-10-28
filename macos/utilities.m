/* Native presenters for bundled KiTTYgen and Kageant; key operations stay in PuTTY. */
#import <AppKit/AppKit.h>
#include "putty.h"
#include "language.h"
#include "transfer.h"
#include "utilities.h"
#include <unistd.h>
#include <signal.h>
static NSString *L(NSString *en,NSString *ru){return kr_mac_localize_string(en);}
static NSMutableArray *generators;
@interface KeyGenerator:NSObject <NSWindowDelegate>
@property(nonatomic,strong) NSWindow *window;
@property(nonatomic,strong) NSPopUpButton *type;
@property(nonatomic,strong) NSPopUpButton *format;
@property(nonatomic,strong) NSTextField *bits;
@property(nonatomic,strong) NSTextField *comment;
@property(nonatomic,strong) NSTextField *fileLabel;
@property(nonatomic,strong) NSURL *key;
@property(nonatomic,strong) NSTextView *publicText;
@property(nonatomic,strong) NSPopUpButton *language;
@end
@implementation KeyGenerator
- (void)windowWillClose:(NSNotification*)note{[NSNotificationCenter.defaultCenter removeObserver:self];self.window.delegate=nil;self.window=nil;[generators removeObject:self];}
- (void)languageChanged:(NSPopUpButton*)picker {if(kr_mac_set_language((int)picker.indexOfSelectedItem))[NSNotificationCenter.defaultCenter postNotificationName:@"KiTTYNGLanguageChanged" object:nil];}
- (void)uiLanguageChanged:(NSNotification*)note{[self.language selectItemAtIndex:kr_mac_language()];kr_mac_localize_view(self.window.contentView);if(!self.key)self.fileLabel.stringValue=L(@"No key loaded",@"Ключ не загружен");}
- (void)refreshPublic {
    if(!self.key)return;__weak KeyGenerator *weak=self;
    kr_mac_run_helper(@"kittygen",@[self.key.path,@"-L"],L(@"KiTTYgen — public key",@"KiTTYgen — публичный ключ"),^(int code,NSString *output){KeyGenerator *g=weak;if(g&&code==0)g.publicText.string=output;});
}
- (void)load:(id)sender{NSOpenPanel *panel=[NSOpenPanel openPanel];if([panel runModal]==NSModalResponseOK){NSURL *candidate=panel.URL;__weak KeyGenerator *weak=self;kr_mac_run_helper(@"kittygen",@[candidate.path,@"-L"],L(@"KiTTYgen — public key",@"KiTTYgen — публичный ключ"),^(int code,NSString *output){KeyGenerator *g=weak;if(g&&code==0){g.key=candidate;g.fileLabel.stringValue=candidate.path;g.publicText.string=output;}});}}
- (void)typeChanged:(id)sender{NSString *kind=self.type.selectedItem.representedObject;self.bits.enabled=![kind isEqualToString:@"ed25519"];self.bits.stringValue=[kind isEqualToString:@"ecdsa"]?@"256":[kind isEqualToString:@"rsa"]?@"3072":@"255";}
- (void)generate:(id)sender {
    NSSavePanel *panel=[NSSavePanel savePanel];panel.nameFieldStringValue=@"key.ppk";if([panel runModal]!=NSModalResponseOK)return;NSString *kind=self.type.selectedItem.representedObject;NSMutableArray *args=[NSMutableArray arrayWithObjects:@"-t",kind,@"-o",panel.URL.path,@"-O",@"private",nil];
    if(![kind isEqualToString:@"ed25519"]){NSInteger bits=self.bits.integerValue;if(bits<256||bits>16384||([kind isEqualToString:@"rsa"]&&bits<2048)||([kind isEqualToString:@"ecdsa"]&&bits!=256&&bits!=384&&bits!=521)){NSBeep();return;}[args addObjectsFromArray:@[@"-b",self.bits.stringValue]];}if(self.comment.stringValue.length)[args addObjectsFromArray:@[@"-C",self.comment.stringValue]];
    __weak KeyGenerator *weak=self;NSURL *target=panel.URL;kr_mac_run_helper(@"kittygen",args,L(@"KiTTYgen — generate key",@"KiTTYgen — создание ключа"),^(int code,NSString *output){KeyGenerator *g=weak;if(g&&code==0){g.key=target;g.fileLabel.stringValue=target.path;[g refreshPublic];}});
}
- (void)exportKey:(id)sender {
    if(!self.key){NSBeep();return;}NSSavePanel *panel=[NSSavePanel savePanel];panel.nameFieldStringValue=@"key-export";if([panel runModal]!=NSModalResponseOK)return;NSString *format=self.format.selectedItem.representedObject;
    kr_mac_run_helper(@"kittygen",@[self.key.path,@"-O",format,@"-o",panel.URL.path],L(@"KiTTYgen — export key",@"KiTTYgen — экспорт ключа"),nil);
}
- (void)changePassphrase:(id)sender {
    if(!self.key){NSBeep();return;}NSSavePanel *panel=[NSSavePanel savePanel];panel.nameFieldStringValue=self.key.lastPathComponent;if([panel runModal]!=NSModalResponseOK)return;NSURL *target=panel.URL;__weak KeyGenerator *weak=self;
    kr_mac_run_helper(@"kittygen",@[self.key.path,@"-P",@"-O",@"private",@"-o",target.path],L(@"KiTTYgen — change passphrase",@"KiTTYgen — изменение парольной фразы"),^(int code,NSString *output){KeyGenerator *g=weak;if(g&&code==0){g.key=target;g.fileLabel.stringValue=target.path;}});
}
- (void)copyPublic:(id)sender{[NSPasteboard.generalPasteboard clearContents];[NSPasteboard.generalPasteboard setString:self.publicText.string forType:NSPasteboardTypeString];}
@end
static NSButton *button(NSView *view,NSString *title,id target,SEL action,NSRect frame){NSButton *b=[NSButton buttonWithTitle:title target:target action:action];b.frame=frame;[view addSubview:b];return b;}
void kr_mac_show_keygen(void){
    if(!generators)generators=[NSMutableArray new];KeyGenerator *g=[KeyGenerator new];[generators addObject:g];g.window=[[NSWindow alloc]initWithContentRect:NSMakeRect(0,0,820,500) styleMask:NSWindowStyleMaskTitled|NSWindowStyleMaskClosable|NSWindowStyleMaskResizable backing:NSBackingStoreBuffered defer:NO];g.window.releasedWhenClosed=NO;g.window.delegate=g;[NSNotificationCenter.defaultCenter addObserver:g selector:@selector(uiLanguageChanged:) name:@"KiTTYNGLanguageChanged" object:nil];g.window.title=@"KiTTYgen";g.window.minSize=NSMakeSize(740,460);NSView *v=g.window.contentView;
    g.fileLabel=[NSTextField labelWithString:L(@"No key loaded",@"Ключ не загружен")];g.fileLabel.frame=NSMakeRect(20,440,570,30);g.fileLabel.lineBreakMode=NSLineBreakByTruncatingMiddle;g.fileLabel.identifier=@"KiTTYNG.RawData";[v addSubview:g.fileLabel];
    g.language=[[NSPopUpButton alloc]initWithFrame:NSMakeRect(620,445,180,28) pullsDown:NO];[g.language addItemsWithTitles:@[@"English",@"Русский"]];[g.language selectItemAtIndex:kr_mac_language()];g.language.target=g;g.language.action=@selector(languageChanged:);[v addSubview:g.language];
    NSScrollView *scroll=[[NSScrollView alloc]initWithFrame:NSMakeRect(20,230,780,195)];scroll.hasVerticalScroller=YES;scroll.autoresizingMask=NSViewWidthSizable|NSViewHeightSizable;g.publicText=[[NSTextView alloc]initWithFrame:scroll.bounds];g.publicText.editable=NO;g.publicText.identifier=@"KiTTYNG.RawData";g.publicText.font=[NSFont monospacedSystemFontOfSize:12 weight:NSFontWeightRegular];scroll.documentView=g.publicText;[v addSubview:scroll];
    g.type=[[NSPopUpButton alloc]initWithFrame:NSMakeRect(20,185,190,28) pullsDown:NO];for(NSArray *entry in @[@[@"Ed25519",@"ed25519"],@[@"RSA",@"rsa"],@[@"ECDSA",@"ecdsa"]]){[g.type addItemWithTitle:entry[0]];g.type.lastItem.representedObject=entry[1];}[v addSubview:g.type];
    g.bits=[[NSTextField alloc]initWithFrame:NSMakeRect(225,185,100,28)];g.bits.stringValue=@"255";g.bits.enabled=NO;g.type.target=g;g.type.action=@selector(typeChanged:);[v addSubview:g.bits];g.comment=[[NSTextField alloc]initWithFrame:NSMakeRect(345,185,455,28)];g.comment.placeholderString=L(@"Key comment",@"Комментарий ключа");[v addSubview:g.comment];
    button(v,L(@"Load…",@"Загрузить…"),g,@selector(load:),NSMakeRect(20,138,135,32));button(v,L(@"Generate…",@"Создать…"),g,@selector(generate:),NSMakeRect(170,138,135,32));button(v,L(@"Change passphrase…",@"Парольная фраза…"),g,@selector(changePassphrase:),NSMakeRect(320,138,235,32));button(v,L(@"Copy public key",@"Копировать публичный ключ"),g,@selector(copyPublic:),NSMakeRect(570,138,230,32));
    g.format=[[NSPopUpButton alloc]initWithFrame:NSMakeRect(20,85,535,28) pullsDown:NO];for(NSArray *entry in @[@[@"PuTTY PPK",@"private"],@[@"OpenSSH private",@"private-openssh-new"],@[@"OpenSSH public",@"public-openssh"],@[@"RFC 4716 public",@"public"]]){[g.format addItemWithTitle:entry[0]];g.format.lastItem.representedObject=entry[1];}[v addSubview:g.format];button(v,L(@"Export…",@"Экспорт…"),g,@selector(exportKey:),NSMakeRect(570,80,230,32));
    NSTextField *notice=[NSTextField wrappingLabelWithString:L(@"PuTTY key generator by Simon Tatham and contributors. KiTTY by Cyril Dupont. Private key operations use the bundled Universal KiTTYgen.",@"Генератор ключей PuTTY: Simon Tatham и участники проекта. KiTTY: Cyril Dupont. Операции с ключами выполняет встроенный Universal KiTTYgen.")];notice.frame=NSMakeRect(20,20,780,45);[v addSubview:notice];[g.window center];[g.window makeKeyAndOrderFront:nil];
}
/* Called only by the guarded native smoke-test command. */
int kr_mac_test_keygen_ui(void){
    kr_mac_show_keygen();KeyGenerator *g=generators.lastObject;[g.window orderOut:nil];
    g.comment.stringValue=@"Generate — synthetic key comment";[g.type selectItemAtIndex:1];[g typeChanged:nil];
    g.key=[NSURL fileURLWithPath:@"/synthetic/English/key.ppk"];g.fileLabel.stringValue=g.key.path;g.publicText.string=@"ssh-ed25519 synthetic-public-fixture";
    int status=0;
    for(int round=0;round<6;round++){
        if(!kr_mac_set_language(round%2)){status=1;break;}
        [NSNotificationCenter.defaultCenter postNotificationName:@"KiTTYNGLanguageChanged" object:nil];
        if(![[g.language itemAtIndex:0].title isEqualToString:@"English"]||![[g.language itemAtIndex:1].title isEqualToString:@"Русский"]||g.language.indexOfSelectedItem!=round%2||g.type.indexOfSelectedItem!=1||g.bits.integerValue!=3072||![g.comment.stringValue isEqualToString:@"Generate — synthetic key comment"]||![g.fileLabel.stringValue isEqualToString:@"/synthetic/English/key.ppk"]||![g.publicText.string isEqualToString:@"ssh-ed25519 synthetic-public-fixture"]){status=1;break;}
    }
    [g.window close];if(!status)puts("PASS: native KiTTYgen language round-trips preserve language choices, key path, comment, algorithm, bits and public data");return status;
}
@interface AgentController:NSObject <NSMenuDelegate>
@property(nonatomic,strong) NSStatusItem *status;
@property(nonatomic,strong) NSTask *task;
@property(nonatomic,strong) NSString *directory;
@property(nonatomic,strong) NSString *socket;
@property(nonatomic,strong) NSString *previousSocket;
@end
@implementation AgentController
- (void)start:(id)sender {
    if(self.task.running)return;if(!self.previousSocket){const char *old=getenv("SSH_AUTH_SOCK");self.previousSocket=old?[NSString stringWithUTF8String:old]:@"";}self.directory=[NSString stringWithFormat:@"/tmp/kitty-ng.%u.%d",getuid(),getpid()];char *failure=make_dir_and_check_ours(self.directory.UTF8String);if(failure){sfree(failure);NSBeep();return;}self.socket=[self.directory stringByAppendingPathComponent:@"agent.sock"];setenv("SSH_AUTH_SOCK",self.socket.UTF8String,1);
    NSString *gui=[NSBundle.mainBundle.bundleURL.path stringByAppendingPathComponent:@"Contents/MacOS/kitty-ng"],*pid=[NSString stringWithFormat:@"%d",getpid()];
    __weak AgentController *weak=self;NSString *socket=self.socket;self.task=kr_mac_run_helper(@"kageant",@[@"--symlink",self.socket,@"--exec",gui,@"--agent-watch",pid],L(@"Kageant — agent",@"Kageant — агент"),^(int code,NSString *output){AgentController *a=weak;if(a&&!a.task.running&&[a.socket isEqualToString:socket]){unlink(socket.fileSystemRepresentation);rmdir(a.directory.fileSystemRepresentation);[a restoreSocket];}});if(!self.task)[self restoreSocket];
}
- (void)restoreSocket{if(self.previousSocket.length)setenv("SSH_AUTH_SOCK",self.previousSocket.UTF8String,1);else unsetenv("SSH_AUTH_SOCK");}
- (void)stop:(id)sender {if(self.task.running){kill(-self.task.processIdentifier,SIGTERM);[self.task terminate];}if(self.socket)unlink(self.socket.fileSystemRepresentation);if(self.directory)rmdir(self.directory.fileSystemRepresentation);[self restoreSocket];}
- (void)ready:(SEL)action attempt:(int)attempt {if(!self.task.running)return;if(access(self.socket.fileSystemRepresentation,F_OK)==0){if(action==@selector(add:))[self add:nil];else [self list:nil];return;}if(attempt<50)dispatch_after(dispatch_time(DISPATCH_TIME_NOW,100*NSEC_PER_MSEC),dispatch_get_main_queue(),^{[self ready:action attempt:attempt+1];});else NSBeep();}
- (void)add:(id)sender{if(!self.task.running){[self start:nil];[self ready:@selector(add:) attempt:0];return;}NSOpenPanel *panel=[NSOpenPanel openPanel];panel.allowsMultipleSelection=YES;if([panel runModal]!=NSModalResponseOK)return;NSMutableArray *args=[NSMutableArray arrayWithObject:@"-a"];for(NSURL *url in panel.URLs)[args addObject:url.path];kr_mac_run_helper(@"kageant",args,L(@"Kageant — add keys",@"Kageant — добавление ключей"),nil);}
- (void)list:(id)sender{if(!self.task.running){[self start:nil];[self ready:@selector(list:) attempt:0];return;}kr_mac_run_helper(@"kageant",@[@"-l"],L(@"Kageant — loaded keys",@"Kageant — загруженные ключи"),nil);}
- (void)removeAll:(id)sender{NSAlert *a=[NSAlert new];a.messageText=L(@"Remove all keys from the agent?",@"Удалить все ключи из агента?");[a addButtonWithTitle:L(@"Cancel",@"Отмена")];[a addButtonWithTitle:L(@"Remove",@"Удалить")];if([a runModal]==NSAlertSecondButtonReturn)kr_mac_run_helper(@"kageant",@[@"-D"],L(@"Kageant — remove keys",@"Kageant — удаление ключей"),nil);}
- (void)show:(id)sender{kr_mac_show_windows();[NSApp activateIgnoringOtherApps:YES];}
- (void)keygen:(id)sender{kr_mac_show_keygen();}
- (void)language:(NSMenuItem*)sender{if(kr_mac_set_language([sender.representedObject intValue]))[NSNotificationCenter.defaultCenter postNotificationName:@"KiTTYNGLanguageChanged" object:nil];}
- (void)menuWillOpen:(NSMenu*)menu {
    [menu removeAllItems];NSArray *items=@[@[L(@"Show KiTTY-ng",@"Показать KiTTY-ng"),NSStringFromSelector(@selector(show:))],@[L(@"Start agent",@"Запустить агент"),NSStringFromSelector(@selector(start:))],@[L(@"Loaded keys",@"Загруженные ключи"),NSStringFromSelector(@selector(list:))],@[L(@"Add keys…",@"Добавить ключи…"),NSStringFromSelector(@selector(add:))],@[L(@"Remove all keys",@"Удалить все ключи"),NSStringFromSelector(@selector(removeAll:))],@[L(@"Stop agent",@"Остановить агент"),NSStringFromSelector(@selector(stop:))],@[@"KiTTYgen",NSStringFromSelector(@selector(keygen:))]];
    for(NSArray *item in items){NSMenuItem *entry=[[NSMenuItem alloc]initWithTitle:item[0] action:NSSelectorFromString(item[1]) keyEquivalent:@""];entry.target=self;[menu addItem:entry];}
    NSMenuItem *lang=[[NSMenuItem alloc]initWithTitle:L(@"Language",@"Язык") action:nil keyEquivalent:@""];NSMenu *sub=[NSMenu new];for(int i=0;i<2;i++){NSMenuItem *item=[[NSMenuItem alloc]initWithTitle:i?@"Русский":@"English" action:@selector(language:) keyEquivalent:@""];item.target=self;item.representedObject=@(i);item.state=i==kr_mac_language()?NSControlStateValueOn:NSControlStateValueOff;[sub addItem:item];}lang.submenu=sub;[menu addItem:lang];
}
@end
static AgentController *agent;
void kr_mac_install_status_menu(void){
    if(agent)return;agent=[AgentController new];agent.status=[NSStatusBar.systemStatusBar statusItemWithLength:NSVariableStatusItemLength];NSImage *icon=[[NSImage alloc]initWithContentsOfFile:[NSBundle.mainBundle pathForResource:@"kitty-ng" ofType:@"icns"]];icon.size=NSMakeSize(18,18);agent.status.button.image=icon;agent.status.button.toolTip=@"KiTTY-ng / Kageant";NSMenu *menu=[NSMenu new];menu.delegate=agent;agent.status.menu=menu;[agent menuWillOpen:menu];
    [NSNotificationCenter.defaultCenter addObserverForName:NSApplicationWillTerminateNotification object:nil queue:nil usingBlock:^(NSNotification *n){[agent stop:nil];}];
}
void kr_mac_show_agent(void){kr_mac_install_status_menu();[agent start:nil];[agent ready:@selector(list:) attempt:0];}
