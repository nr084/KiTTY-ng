/* Native common session editor. Typed controls plus remaining stored options. */
#import <AppKit/AppKit.h>
#include "putty.h"
#include "storage.h"
#include "portable.h"
#include "language.h"
#include "cli-language.h"
#include "editor.h"
#include "client.h"
#include <limits.h>
static NSMutableArray *editors;
static NSString *S(const char *s){return [NSString stringWithUTF8String:s]?:[[NSString alloc]initWithBytes:s length:strlen(s) encoding:NSWindowsCP1251StringEncoding]?:@"";}
static NSString *L(NSString *en,NSString *ru){return kr_mac_localize_string(en);}
#define CONF_ENUM(name,...) extern const ConfSaveEnumType conf_enum_##name;
#include "conf-enums.h"
#undef CONF_ENUM
static const char *enumName(const ConfSaveEnumType *type,int value){
#define VALUE(c,s) {s,#c}
#define VALUE_OBSOLETE(c,s) {s,#c}
#define CONF_ENUM(name,...) if(type==&conf_enum_##name){static const struct{int value;const char *label;}labels[]={__VA_ARGS__};for(size_t i=0;i<lenof(labels);i++)if(labels[i].value==value)return labels[i].label;return "";}
#include "conf-enums.h"
#undef CONF_ENUM
#undef VALUE
#undef VALUE_OBSOLETE
return "";
}
static NSString *enumTitle(const char *name){NSString *text=S(name);if([text isEqualToString:@"FORCE_ON"])return L(@"Enabled",@"Включено");if([text isEqualToString:@"FORCE_OFF"])return L(@"Disabled",@"Выключено");if([text isEqualToString:@"AUTO"])return L(@"Automatic",@"Автоматически");return text;}
@interface EditorPanel:NSView
@end
@implementation EditorPanel
- (BOOL)isFlipped{return YES;}
@end
@interface MacEditor:NSObject <NSWindowDelegate,NSTableViewDataSource,NSTableViewDelegate,NSOutlineViewDataSource,NSOutlineViewDelegate>
@property(assign) Conf *conf;
@property(assign) settings_r *reader;
@property(nonatomic,strong) NSData *source;
@property(nonatomic,strong) NSWindow *window;
@property(nonatomic,strong) NSTextField *name;
@property(nonatomic,strong) NSString *original;
@property(nonatomic,strong) NSMutableDictionary *fields;
@property(nonatomic,strong) NSMutableArray *advanced;
@property(nonatomic,strong) NSMutableDictionary *changes;
@property(nonatomic,strong) NSSecureTextField *password;
@property(nonatomic,strong) NSButton *clearPassword;
@property(nonatomic,strong) NSArray *optionTree;
@property(nonatomic,strong) NSOutlineView *tree;
@property(nonatomic) BOOL duplicate;
@property(nonatomic) BOOL quick;
@end
@implementation MacEditor
- (NSInteger)outlineView:(NSOutlineView*)view numberOfChildrenOfItem:(id)item{return item?[item[@"children"]count]:self.optionTree.count;}
- (id)outlineView:(NSOutlineView*)view child:(NSInteger)index ofItem:(id)item{return item?item[@"children"][index]:self.optionTree[index];}
- (BOOL)outlineView:(NSOutlineView*)view isItemExpandable:(id)item{return item[@"children"]!=nil;}
- (id)outlineView:(NSOutlineView*)view objectValueForTableColumn:(NSTableColumn*)column byItem:(id)item{if([column.identifier isEqualToString:@"name"])return item[@"name"];if(item[@"choices"]){NSUInteger index=[item[@"choices"]indexOfObject:item[@"value"]];return @(index==NSNotFound?0:index);}if([item[@"boolean"]boolValue])return @([item[@"value"]intValue]!=0);return item[@"value"];}
- (BOOL)outlineView:(NSOutlineView*)view shouldEditTableColumn:(NSTableColumn*)column item:(id)item{return !item[@"children"]&&[column.identifier isEqualToString:@"value"];}
- (void)outlineView:(NSOutlineView*)view setObjectValue:(id)value forTableColumn:(NSTableColumn*)column byItem:(id)item{if([self outlineView:view shouldEditTableColumn:column item:item]){if(item[@"choices"]&&([value integerValue]<0||[value integerValue]>=[item[@"choices"]count]))return;NSString *stored=item[@"choices"]?item[@"choices"][[value integerValue]]:[item[@"boolean"]boolValue]?([value boolValue]?@"1":@"0"):value?:@"";item[@"value"]=stored;self.changes[item[@"name"]]=stored;}}
- (NSCell *)outlineView:(NSOutlineView*)view dataCellForTableColumn:(NSTableColumn*)column item:(id)item {if(![column.identifier isEqualToString:@"value"]||item[@"children"])return column.dataCell;if(item[@"choices"]){NSPopUpButtonCell *cell=[[NSPopUpButtonCell alloc]initTextCell:@"" pullsDown:NO];[cell addItemsWithTitles:item[@"titles"]];return cell;}if([item[@"boolean"]boolValue]){NSButtonCell *cell=[[NSButtonCell alloc]initTextCell:@""];cell.buttonType=NSButtonTypeSwitch;return cell;}return column.dataCell;}
- (NSInteger)numberOfRowsInTableView:(NSTableView*)view{return self.advanced.count;}
- (id)tableView:(NSTableView*)view objectValueForTableColumn:(NSTableColumn*)column row:(NSInteger)row{return self.advanced[row][column.identifier];}
- (void)tableView:(NSTableView*)view setObjectValue:(id)value forTableColumn:(NSTableColumn*)column row:(NSInteger)row{if([column.identifier isEqualToString:@"value"]){NSMutableDictionary *item=self.advanced[row];if(item[@"choices"]&&([value integerValue]<0||[value integerValue]>=[item[@"choices"]count]))return;NSString *stored=item[@"choices"]?item[@"choices"][[value integerValue]]:[item[@"boolean"]boolValue]?([value boolValue]?@"1":@"0"):value?:@"";item[@"value"]=stored;self.changes[item[@"name"]]=stored;}}
- (void)uiLanguageChanged:(NSNotification*)note{kr_mac_localize_view(self.window.contentView);self.window.title=kr_mac_localize_string(self.window.title);NSMutableArray *nodes=[NSMutableArray new];for(NSDictionary *node in self.optionTree){NSMutableDictionary *copy=[node mutableCopy];copy[@"name"]=kr_mac_localize_string(copy[@"name"]);for(NSMutableDictionary *row in copy[@"children"]){if(row[@"titles"]){NSMutableArray *titles=[NSMutableArray new];for(NSString *title in row[@"titles"]){NSArray *parts=[title componentsSeparatedByString:@" — "];[titles addObject:parts.count==2?[NSString stringWithFormat:@"%@ — %@",parts[0],kr_mac_localize_string(parts[1])]:title];}row[@"titles"]=titles;}}[nodes addObject:copy];}self.optionTree=nodes;[self.tree reloadData];}
- (void)cancel:(id)sender{[self.window close];}
- (void)windowWillClose:(NSNotification*)note{if(self.conf){conf_free(self.conf);self.conf=NULL;}close_settings_r(self.reader);self.reader=NULL;[NSNotificationCenter.defaultCenter removeObserver:self];self.window.delegate=nil;self.window=nil;[editors removeObject:self];}
- (void)selectFile:(NSButton*)sender{NSTextField *field=self.fields[@(sender.tag)];NSOpenPanel *panel=[NSOpenPanel openPanel];if([panel runModal]==NSModalResponseOK)field.stringValue=panel.URL.path;}
- (void)save:(id)sender{
    if(![self.window makeFirstResponder:nil])return;
    NSString *name=[self.name.stringValue stringByTrimmingCharactersInSet:NSCharacterSet.whitespaceAndNewlineCharacterSet];if(!self.quick&&!name.length)return;
    if(self.duplicate&&[name isEqualToString:self.original]){NSAlert *a=[NSAlert new];a.messageText=L(@"Session already exists",@"Сеанс уже существует");[a runModal];return;}
    NSString *target=nil;
    if(!self.quick){
    char *path=kr_mac_profile_path(name.UTF8String);if(!path){NSBeep();return;}target=S(path);sfree(path);
    if(![name isEqualToString:self.original]&&[NSFileManager.defaultManager fileExistsAtPath:target]){NSAlert *a=[NSAlert new];a.messageText=L(@"Session already exists",@"Сеанс уже существует");[a runModal];return;}
    if(self.original.length){char *current=kr_mac_profile_path(self.original.UTF8String);NSData *data=current?[NSData dataWithContentsOfFile:S(current)]:nil;sfree(current);if(!data||![data isEqualToData:self.source]){NSAlert *a=[NSAlert new];a.messageText=L(@"Profile changed outside this editor; reopen it",@"Профиль изменён извне; откройте его заново");[a runModal];return;}}
    }
    Conf *candidate=conf_copy(self.conf);
    if(self.changes.count){settings_r *overlay=kr_mac_copy_reader(self.reader);for(NSString *key in self.changes)kr_mac_override_read(overlay,key.UTF8String,[self.changes[key]UTF8String]);load_open_settings(overlay,candidate);close_settings_r(overlay);conf_set_str(candidate,CONF_password,conf_get_str(self.conf,CONF_password));conf_set_bool(candidate,CONF_kr_password_decoded,conf_get_bool(self.conf,CONF_kr_password_decoded));}

    for(NSNumber *number in self.fields){int key=number.intValue;id field=self.fields[number];NSString *value=[field isKindOfClass:NSTextView.class]?((NSTextView*)field).string:((NSControl*)field).stringValue;int type=conf_key_info[key].value_type;
        if([field isKindOfClass:NSPopUpButton.class])conf_set_int(candidate,key,[((NSPopUpButton*)field).selectedItem.representedObject intValue]);
        else if(type==CONF_TYPE_BOOL)conf_set_bool(candidate,key,((NSButton*)field).state==NSControlStateValueOn);
        else if(type==CONF_TYPE_INT){NSScanner *scanner=[NSScanner scannerWithString:value];long long integer;if(![scanner scanLongLong:&integer]||!scanner.isAtEnd||integer<INT_MIN||integer>INT_MAX){conf_free(candidate);NSAlert *a=[NSAlert new];a.messageText=L(@"Invalid integer value",@"Некорректное целое значение");a.informativeText=value;[a runModal];return;}conf_set_int(candidate,key,(int)integer);}
        else if(type==CONF_TYPE_FILENAME){Filename *file=filename_from_str(value.UTF8String);conf_set_filename(candidate,key,file);filename_free(file);}
        else if(type==CONF_TYPE_FONT){FontSpec *font=fontspec_new(value.UTF8String);conf_set_fontspec(candidate,key,font);fontspec_free(font);}
        else if(type==CONF_TYPE_STR_AMBI)conf_set_utf8(candidate,key,value.UTF8String);
        else conf_set_str(candidate,key,value.UTF8String);
    }
    if(conf_get_int(candidate,CONF_port)<1||conf_get_int(candidate,CONF_port)>65535||conf_get_int(candidate,CONF_width)<20||conf_get_int(candidate,CONF_width)>1000||conf_get_int(candidate,CONF_height)<8||conf_get_int(candidate,CONF_height)>1000){conf_free(candidate);NSAlert *a=[NSAlert new];a.messageText=L(@"Check port and terminal size",@"Проверьте порт и размер терминала");[a runModal];return;}
    const int nonnegative[]={CONF_savelines,CONF_initdelay,CONF_kr_command_delay,CONF_bcdelay,CONF_kr_reconnect_delay};for(size_t i=0;i<lenof(nonnegative);i++){int value=conf_get_int(candidate,nonnegative[i]);int maximum=nonnegative[i]==CONF_savelines?1000000:nonnegative[i]==CONF_kr_reconnect_delay?86400:nonnegative[i]==CONF_bcdelay?1000:60000;if(value<0||value>maximum){conf_free(candidate);NSAlert *a=[NSAlert new];a.messageText=L(@"Check history size and automation delays",@"Проверьте размер истории и задержки автоматизации");[a runModal];return;}}
    if(conf_get_int(candidate,CONF_proxy_port)<1||conf_get_int(candidate,CONF_proxy_port)>65535||conf_get_int(candidate,CONF_serspeed)<1){conf_free(candidate);NSBeep();return;}
    if(self.clearPassword.state==NSControlStateValueOn){conf_set_str(candidate,CONF_password,"");conf_set_str(candidate,CONF_kr_password_encoded,"");conf_set_bool(candidate,CONF_kr_password_decoded,true);}
    else if(strcmp(conf_get_str(candidate,CONF_password),self.password.stringValue.UTF8String)){conf_set_str(candidate,CONF_password,self.password.stringValue.UTF8String);conf_set_bool(candidate,CONF_kr_password_decoded,true);}
    if(self.quick){
        if(!conf_launchable(candidate)){conf_free(candidate);NSAlert *a=[NSAlert new];a.messageText=L(@"Enter a host name",@"Введите имя сервера");[a runModal];return;}
        conf_set_str(candidate,CONF_kr_session_name,"");conf_set_bool(candidate,CONF_save_windowpos,false);conf_set_bool(candidate,CONF_saveonexit,false);
        kr_mac_open_conf(candidate);conf_free(candidate);[self.window close];return;
    }
    if(self.original.length&&![name isEqualToString:self.original]){char *old=kr_mac_profile_path(self.original.UTF8String);NSString *parent=target.stringByDeletingLastPathComponent;[NSFileManager.defaultManager createDirectoryAtPath:parent withIntermediateDirectories:YES attributes:nil error:nil];BOOL copied=old&&[NSFileManager.defaultManager copyItemAtPath:S(old) toPath:target error:nil];sfree(old);if(!copied){conf_free(candidate);NSBeep();return;}}
    char *failure=save_settings(name.UTF8String,candidate);conf_free(candidate);if(failure||!kr_mac_write_succeeded()){NSAlert *a=[NSAlert new];a.messageText=L(@"Unable to save session",@"Не удалось сохранить сеанс");a.informativeText=failure?S(kr_mac_translate(failure)):@"";sfree(failure);[a runModal];return;}
    [NSNotificationCenter.defaultCenter postNotificationName:@"KiTTYNGProfilesChanged" object:nil];[self.window close];
}
@end
static NSDictionary *F(int key,NSString *en,NSString *ru){return @{@"key":@(key),@"label":L(en,ru)};}
static NSArray *choices(int key){
    if(key==CONF_protocol)return @[@[@"SSH",@(PROT_SSH)],@[@"Telnet",@(PROT_TELNET)],@[@"Raw",@(PROT_RAW)],@[L(@"Serial",@"Последовательный порт"),@(PROT_SERIAL)]];
    if(key==CONF_close_on_exit)return @[@[L(@"Never",@"Никогда"),@(FORCE_OFF)],@[L(@"Always",@"Всегда"),@(FORCE_ON)],@[L(@"On clean exit",@"При нормальном выходе"),@(AUTO)]];
    if(key==CONF_proxy_type)return @[@[@"None",@(PROXY_NONE)],@[@"SOCKS4",@(PROXY_SOCKS4)],@[@"SOCKS5",@(PROXY_SOCKS5)],@[@"HTTP",@(PROXY_HTTP)],@[@"Telnet",@(PROXY_TELNET)],@[@"Local command",@(PROXY_CMD)]];
    if(key==CONF_winscpprot)return @[@[@"SFTP",@1],@[@"SCP",@0]];return nil;
}
static MacEditor *open_profile_editor(const char *name,bool existing,Conf *configuration){
    if(!editors)editors=[NSMutableArray new];MacEditor *editor=[MacEditor new];editor.conf=configuration?conf_copy(configuration):conf_new();bool loaded=configuration||load_settings(existing?name:"Default Settings",editor.conf);if(existing&&!loaded){conf_free(editor.conf);editor.conf=NULL;NSAlert *a=[NSAlert new];a.messageText=L(@"Unable to read this portable profile",@"Не удалось прочитать этот портативный профиль");[a runModal];return nil;}if(!existing&&conf_get_int(editor.conf,CONF_protocol)==PROT_SSH&&conf_get_int(editor.conf,CONF_port)<=0)conf_set_int(editor.conf,CONF_port,22);editor.reader=configuration?kr_mac_snapshot_settings(editor.conf,save_open_settings):open_settings_r(existing?name:"Default Settings");char *source=existing?kr_mac_profile_path(name):NULL;editor.source=source?[NSData dataWithContentsOfFile:S(source)]:nil;sfree(source);editor.original=existing?S(name):@"";editor.fields=[NSMutableDictionary new];editor.changes=[NSMutableDictionary new];editor.advanced=[NSMutableArray new];
    editor.window=[[NSWindow alloc]initWithContentRect:NSMakeRect(0,0,920,720) styleMask:NSWindowStyleMaskTitled|NSWindowStyleMaskClosable|NSWindowStyleMaskResizable backing:NSBackingStoreBuffered defer:NO];editor.window.releasedWhenClosed=NO;editor.window.delegate=editor;[NSNotificationCenter.defaultCenter addObserver:editor selector:@selector(uiLanguageChanged:) name:@"KiTTYNGLanguageChanged" object:nil];editor.window.title=L(@"KiTTY-ng — session settings",@"KiTTY-ng — настройка сеанса");editor.window.minSize=NSMakeSize(800,600);NSView *root=editor.window.contentView;
    NSTextField *label=[NSTextField labelWithString:L(@"Session name / folder",@"Имя сеанса / папка")];label.frame=NSMakeRect(20,675,220,25);label.autoresizingMask=NSViewMinYMargin;[root addSubview:label];editor.name=[[NSTextField alloc]initWithFrame:NSMakeRect(245,672,650,28)];editor.name.stringValue=S(name);editor.name.autoresizingMask=NSViewWidthSizable|NSViewMinYMargin;[root addSubview:editor.name];
    NSTabView *tabs=[[NSTabView alloc]initWithFrame:NSMakeRect(16,80,888,575)];tabs.autoresizingMask=NSViewWidthSizable|NSViewHeightSizable;[root addSubview:tabs];
    NSArray *groups=@[
      @[L(@"Session",@"Сеанс"),@[F(CONF_host,@"Host",@"Сервер"),F(CONF_port,@"Port",@"Порт"),F(CONF_protocol,@"Protocol",@"Протокол"),F(CONF_username,@"User name",@"Пользователь"),F(CONF_close_on_exit,@"Close window on exit",@"Закрытие окна при выходе"),F(CONF_comment,@"Notes",@"Заметки"),F(CONF_serline,@"Serial device",@"Последовательное устройство"),F(CONF_serspeed,@"Baud rate",@"Скорость порта")]],
      @[L(@"SSH",@"SSH"),@[F(CONF_keyfile,@"Private key file",@"Файл закрытого ключа"),F(CONF_detached_cert,@"Detached certificate",@"Отдельный сертификат"),F(CONF_tryagent,@"Use SSH agent",@"Использовать SSH-агент"),F(CONF_agentfwd,@"Forward agent",@"Переадресация агента"),F(CONF_compression,@"Compression",@"Сжатие"),F(CONF_remote_cmd,@"Remote command",@"Удалённая команда"),F(CONF_nopty,@"Do not allocate PTY",@"Не выделять PTY")]],
      @[L(@"Terminal",@"Терминал"),@[F(CONF_width,@"Columns",@"Столбцы"),F(CONF_height,@"Rows",@"Строки"),F(CONF_savelines,@"Scrollback lines",@"Строки истории"),F(CONF_font,@"Mac font and size",@"Шрифт Mac и размер"),F(CONF_boldfont,@"Mac bold font (empty: automatic)",@"Жирный шрифт Mac (пусто: автоматически)"),F(CONF_widefont,@"Mac wide font",@"Широкий шрифт Mac"),F(CONF_wideboldfont,@"Mac wide bold font",@"Широкий жирный шрифт Mac"),F(CONF_line_codepage,@"Character set",@"Кодировка"),F(CONF_wrap_mode,@"Auto wrap",@"Перенос строк"),F(CONF_scrolllines,@"Scroll lines (-1 half page)",@"Прокрутка (-1 полстраницы)")]],
      @[L(@"Automation",@"Автоматика"),@[F(CONF_autocommand,@"Automatic command",@"Автокоманда"),F(CONF_scriptfilecontent,@"Inline startup script",@"Текст стартового скрипта"),F(CONF_kr_script_file,@"UTF-8 script file",@"Файл скрипта UTF-8"),F(CONF_initdelay,@"Initial delay, ms",@"Начальная задержка, мс"),F(CONF_kr_command_delay,@"Line delay, ms",@"Задержка строк, мс"),F(CONF_bcdelay,@"Character delay, ms",@"Задержка символов, мс"),F(CONF_kr_reconnect_delay,@"Reconnect delay, s (0 off)",@"Переподключение, с (0 выкл.)"),F(CONF_wakeup_reconnect,@"Reconnect after wake",@"Переподключать после сна")]],
      @[L(@"Files",@"Файлы"),@[F(CONF_pscpremotedir,@"Remote directory",@"Удалённый каталог"),F(CONF_pscpoptions,@"Kscp options",@"Параметры Kscp"),F(CONF_sftpconnect,@"Alternate user@host:port",@"Другой user@host:port"),F(CONF_winscpprot,@"Transfer protocol",@"Протокол передачи"),F(CONF_scp_auto_pwd,@"Use current remote directory",@"Текущий удалённый каталог")]],
      @[L(@"Proxy",@"Прокси"),@[F(CONF_proxyselection,@"Shared proxy profile",@"Общий профиль прокси"),F(CONF_proxy_type,@"Proxy type",@"Тип прокси"),F(CONF_proxy_host,@"Proxy host",@"Сервер прокси"),F(CONF_proxy_port,@"Proxy port",@"Порт прокси"),F(CONF_proxy_username,@"Proxy user",@"Пользователь прокси"),F(CONF_proxy_password,@"Proxy password",@"Пароль прокси"),F(CONF_proxy_telnet_command,@"Proxy command",@"Команда прокси")]],
      @[L(@"Window",@"Окно"),@[F(CONF_alwaysontop,@"Always on top",@"Поверх окон"),F(CONF_maximize,@"Start maximized",@"Запускать развёрнутым"),F(CONF_fullscreen,@"Start fullscreen",@"Полноэкранный запуск"),F(CONF_transparencynumber,@"Transparency (-1 off)",@"Прозрачность (-1 выкл.)"),F(CONF_save_windowpos,@"Remember Mac position",@"Запоминать положение Mac"),F(CONF_saveonexit,@"Save settings on exit",@"Сохранять настройки при выходе"),F(CONF_sendtotray,@"Start hidden in menu bar",@"Запускать скрытым в строке меню"),F(CONF_foreground_on_bell,@"Bring forward on bell",@"Активировать при звонке"),F(CONF_kr_remote_clipboard,@"Allow OSC 52 clipboard writes",@"Разрешать запись OSC 52"),F(CONF_kr_shortcuts,@"KiTTY keyboard shortcuts",@"Горячие клавиши KiTTY"),F(CONF_kr_hyperlinks,@"Ctrl-click HTTP(S) links",@"Ctrl-щелчок по HTTP(S)")]]
    ];
    for(NSArray *group in groups){NSTabViewItem *tab=[[NSTabViewItem alloc]initWithIdentifier:group[0]];tab.label=group[0];NSScrollView *scroll=[[NSScrollView alloc]initWithFrame:NSMakeRect(0,0,860,530)];scroll.hasVerticalScroller=YES;scroll.autoresizingMask=NSViewWidthSizable|NSViewHeightSizable;EditorPanel *panel=[[EditorPanel alloc]initWithFrame:NSMakeRect(0,0,840,MAX(530,[group[1]count]*46+20))];scroll.documentView=panel;tab.view=scroll;[tabs addTabViewItem:tab];CGFloat y=14;
        for(NSDictionary *definition in group[1]){int key=[definition[@"key"]intValue],type=conf_key_info[key].value_type;NSControl *field=nil;NSArray *options=choices(key);
            if(type==CONF_TYPE_BOOL){NSButton *button=[NSButton checkboxWithTitle:definition[@"label"] target:nil action:nil];button.state=conf_get_bool(editor.conf,key)?NSControlStateValueOn:NSControlStateValueOff;button.frame=NSMakeRect(16,y,800,30);field=button;}
            else{NSTextField *title=[NSTextField labelWithString:definition[@"label"]];title.frame=NSMakeRect(16,y+4,245,24);[panel addSubview:title];
                if(options){NSPopUpButton *picker=[[NSPopUpButton alloc]initWithFrame:NSMakeRect(270,y,535,30) pullsDown:NO];for(NSArray *option in options){[picker addItemWithTitle:option[0]];picker.lastItem.representedObject=option[1];if([option[1]intValue]==conf_get_int(editor.conf,key))[picker selectItem:picker.lastItem];}field=picker;}
                else{NSTextField *input=key==CONF_proxy_password?[[NSSecureTextField alloc]initWithFrame:NSMakeRect(270,y,535,28)]:[[NSTextField alloc]initWithFrame:NSMakeRect(270,y,535,28)];
                    if(type==CONF_TYPE_INT)input.stringValue=[NSString stringWithFormat:@"%d",conf_get_int(editor.conf,key)];else if(type==CONF_TYPE_FILENAME)input.stringValue=S(filename_to_str(conf_get_filename(editor.conf,key)));else if(type==CONF_TYPE_FONT)input.stringValue=S(conf_get_fontspec(editor.conf,key)->name);else if(type==CONF_TYPE_STR_AMBI)input.stringValue=S(conf_get_str_ambi(editor.conf,key,NULL));else input.stringValue=S(conf_get_str(editor.conf,key));field=input;
                    if(type==CONF_TYPE_FILENAME){input.frame=NSMakeRect(270,y,425,28);NSButton *choose=[NSButton buttonWithTitle:L(@"Browse…",@"Выбрать…") target:editor action:@selector(selectFile:)];choose.tag=key;choose.frame=NSMakeRect(705,y,100,28);[panel addSubview:choose];}}
            }
            if(key==CONF_scriptfilecontent||key==CONF_comment){NSScrollView *textScroll=[[NSScrollView alloc]initWithFrame:NSMakeRect(270,y,535,100)];textScroll.hasVerticalScroller=YES;NSTextView *text=[[NSTextView alloc]initWithFrame:textScroll.bounds];text.string=S(conf_get_str(editor.conf,key));text.font=[NSFont monospacedSystemFontOfSize:12 weight:NSFontWeightRegular];textScroll.documentView=text;[panel addSubview:textScroll];editor.fields[@(key)]=text;y+=120;}
            else{[panel addSubview:field];editor.fields[@(key)]=field;y+=46;}
        }
    }
    NSTabViewItem *advanced=[[NSTabViewItem alloc]initWithIdentifier:@"advanced"];advanced.label=L(@"All settings",@"Все настройки");NSScrollView *scroll=[[NSScrollView alloc]initWithFrame:NSMakeRect(0,0,860,530)];scroll.hasVerticalScroller=YES;scroll.autoresizingMask=NSViewWidthSizable|NSViewHeightSizable;NSOutlineView *tree=[[NSOutlineView alloc]initWithFrame:scroll.bounds];tree.dataSource=editor;tree.delegate=editor;editor.tree=tree;
    for(NSString *identifier in @[@"name",@"value"]){NSTableColumn *column=[[NSTableColumn alloc]initWithIdentifier:identifier];column.title=[identifier isEqualToString:@"name"]?L(@"Setting",@"Настройка"):L(@"Value (double-click to edit)",@"Значение (двойной щелчок для изменения)");column.width=[identifier isEqualToString:@"name"]?320:510;column.editable=[identifier isEqualToString:@"value"];[tree addTableColumn:column];if([identifier isEqualToString:@"name"])tree.outlineTableColumn=column;}scroll.documentView=tree;advanced.view=scroll;[tabs addTabViewItem:advanced];
    NSMutableSet *handled=[NSMutableSet set];for(NSNumber *key in editor.fields){const char *keyword=conf_key_info[key.intValue].save_keyword;if(keyword){[handled addObject:S(keyword)];if(conf_key_info[key.intValue].value_type==CONF_TYPE_FONT)[handled addObject:[NSString stringWithFormat:@"Mac%@Name",S(keyword)]];}}[handled addObject:@"Password"];
    settings_r *snapshot=kr_mac_snapshot_settings(editor.conf,save_open_settings);NSArray *categories=@[L(@"Session",@"Сеанс"),L(@"Logging",@"Журналирование"),L(@"Terminal / Keyboard",@"Терминал / Клавиатура"),L(@"Terminal / Translation",@"Терминал / Кодировка"),L(@"Terminal / Behaviour",@"Терминал / Поведение"),L(@"Window / Appearance",@"Окно / Оформление"),L(@"Window / Colours",@"Окно / Цвета"),L(@"Window / Bell",@"Окно / Сигнал"),L(@"Connection",@"Соединение"),L(@"Connection / Proxy",@"Соединение / Прокси"),L(@"Connection / Telnet",@"Соединение / Telnet"),L(@"Connection / Serial",@"Соединение / Последовательный порт"),L(@"Connection / SSH",@"Соединение / SSH"),L(@"SSH / Authentication",@"SSH / Аутентификация"),L(@"SSH / GSSAPI",@"SSH / GSSAPI"),L(@"SSH / Key exchange",@"SSH / Обмен ключами"),L(@"SSH / Tunnels",@"SSH / Туннели"),L(@"KiTTY extensions",@"Расширения KiTTY")];NSMutableArray *nodes=[NSMutableArray new];for(NSString *category in categories)[nodes addObject:@{@"name":category,@"children":[NSMutableArray new]}];
    /* Exported keys cover custom serializers (colours, maps, ordered algorithms),
       not just Conf's scalar metadata. Unknown legacy fields are never exposed as supported options. */
    NSString *raw=nil;settings_r *list=snapshot;
    char *dump=kr_mac_settings_text(list);raw=S(dump);sfree(dump);
    NSMutableSet *seen=[NSMutableSet new];for(NSString *line in [raw componentsSeparatedByString:@"\n"]){NSRange slash=[line rangeOfString:@"\\"];if(slash.location==NSNotFound)continue;NSString *key=[line substringToIndex:slash.location];if([handled containsObject:key]||[seen containsObject:key])continue;[seen addObject:key];char *value=read_setting_s(snapshot,key.UTF8String);if(!value)continue;NSString *lower=key.lowercaseString;NSUInteger category=17;
        if([lower hasPrefix:@"ssh"]||[lower hasPrefix:@"remote"]||[lower hasPrefix:@"cipher"])category=12;
        if([lower containsString:@"auth"]||[lower containsString:@"agent"]||[lower containsString:@"keyfile"]||[lower containsString:@"certificate"])category=13;
        if([lower containsString:@"gss"])category=14;
        if([lower containsString:@"kex"]||[lower containsString:@"rekey"]||[lower containsString:@"hostkey"])category=15;
        if([lower containsString:@"forward"]||[lower hasPrefix:@"x11"])category=16;
        if([lower hasPrefix:@"proxy"])category=9;
        if([lower hasPrefix:@"telnet"]||[lower hasPrefix:@"rlogin"])category=10;
        if([lower hasPrefix:@"serial"])category=11;
        if([lower hasPrefix:@"tcp"]||[lower hasPrefix:@"ping"]||[lower hasPrefix:@"address"]||[lower hasPrefix:@"environment"])category=8;
        if([lower hasPrefix:@"log"])category=1;
        if([lower hasPrefix:@"colour"]||[lower containsString:@"color"])category=6;
        if([lower containsString:@"bell"])category=7;
        if([lower containsString:@"font"]||[lower containsString:@"window"]||[lower containsString:@"cursor"]||[lower hasPrefix:@"border"]||[lower containsString:@"scrollbar"])category=5;
        if([lower containsString:@"codepage"]||[lower containsString:@"utf"]||[lower containsString:@"charset"]||[lower containsString:@"cjk"])category=3;
        if([lower containsString:@"keyboard"]||[lower containsString:@"keypad"]||[lower containsString:@"backspace"]||[lower containsString:@"alt"]||[lower containsString:@"ctrl"]||[lower containsString:@"funky"])category=2;
        if([lower hasPrefix:@"terminal"]||[lower hasPrefix:@"term"]||[lower hasPrefix:@"ansi"]||[lower hasPrefix:@"dec"]||[lower hasPrefix:@"mouse"]||[lower hasPrefix:@"erase"]||[lower hasPrefix:@"wrap"]||[lower hasPrefix:@"scroll"])category=4;
        if([lower hasPrefix:@"host"]||[lower hasPrefix:@"user"]||[lower hasPrefix:@"portnumber"]||[lower hasPrefix:@"protocol"]||[lower hasPrefix:@"closeon"])category=0;
        NSMutableDictionary *row=[@{@"name":key,@"value":S(value)}mutableCopy];sfree(value);for(int id=0;id<N_CONFIG_OPTIONS;id++){const ConfKeyInfo *info=&conf_key_info[id];if(!info->save_keyword||strcmp(info->save_keyword,key.UTF8String)||info->save_custom)continue;if(info->value_type==CONF_TYPE_BOOL)row[@"boolean"]=@YES;if(info->storage_enum){NSMutableArray *values=[NSMutableArray new],*titles=[NSMutableArray new];for(size_t j=0;j<info->storage_enum->nvalues;j++){ConfSaveEnumValue choice=info->storage_enum->values[j];if(choice.obsolete)continue;[values addObject:[NSString stringWithFormat:@"%d",choice.storageval]];[titles addObject:[NSString stringWithFormat:@"%d — %@",choice.storageval,enumTitle(enumName(info->storage_enum,choice.storageval))]];}row[@"choices"]=values;row[@"titles"]=titles;}break;}[nodes[category][@"children"]addObject:row];[editor.advanced addObject:row];}
    close_settings_r(snapshot);editor.optionTree=nodes;[tree reloadData];
    editor.password=[[NSSecureTextField alloc]initWithFrame:NSMakeRect(125,35,300,28)];editor.password.stringValue=S(conf_get_str(editor.conf,CONF_password));NSTextField *passwordLabel=[NSTextField labelWithString:L(@"Password",@"Пароль")];passwordLabel.frame=NSMakeRect(20,38,100,22);[root addSubview:passwordLabel];[root addSubview:editor.password];editor.clearPassword=[NSButton checkboxWithTitle:L(@"Clear saved password",@"Очистить пароль") target:nil action:nil];editor.clearPassword.frame=NSMakeRect(435,34,220,28);[root addSubview:editor.clearPassword];
    NSButton *save=[NSButton buttonWithTitle:L(@"Save",@"Сохранить") target:editor action:@selector(save:)];save.frame=NSMakeRect(760,30,140,34);save.autoresizingMask=NSViewMinXMargin;[root addSubview:save];NSButton *cancel=[NSButton buttonWithTitle:L(@"Cancel",@"Отмена") target:editor action:@selector(cancel:)];cancel.frame=NSMakeRect(660,30,90,34);cancel.autoresizingMask=NSViewMinXMargin;[root addSubview:cancel];[editors addObject:editor];[editor.window center];[editor.window makeKeyAndOrderFront:nil];
    return editor;
}
void kr_mac_edit_profile(const char *name,bool existing){open_profile_editor(name,existing,NULL);}
void kr_mac_duplicate_profile(const char *source,const char *name){
    MacEditor *editor=open_profile_editor(source,true,NULL);
    if(editor){editor.duplicate=YES;editor.name.stringValue=S(name);}
}

void kr_mac_quick_connect(void){kr_mac_quick_connect_conf(NULL);}
void kr_mac_quick_connect_conf(Conf *configuration){
    MacEditor *editor=open_profile_editor("",false,configuration);if(!editor)return;
    editor.quick=YES;editor.name.hidden=YES;
    editor.window.title=L(@"Quick connect",@"Быстрое подключение");
    for(NSView *view in editor.window.contentView.subviews){
        if([view isKindOfClass:NSButton.class]&&((NSButton*)view).action==@selector(save:))((NSButton*)view).title=L(@"Connect",@"Подключиться");
        if([view isKindOfClass:NSTextField.class]&&[[(NSTextField*)view stringValue]isEqualToString:L(@"Session name / folder",@"Имя сеанса / папка")]){((NSTextField*)view).stringValue=L(@"Connect without saving a session",@"Подключение без сохранения сеанса");view.frame=NSMakeRect(20,675,850,25);}
    }
}
