/* Native AppKit browser for shared original KiTTY portable profiles. */
#import <AppKit/AppKit.h>
#import <Foundation/Foundation.h>
#include <stdio.h>
#include <string.h>
#include <sys/event.h>
#include <unistd.h>
#include "version.h"
#ifdef KITTY_NG_CLIENT
#include "client.h"
#include "putty.h"
#include "language.h"
#include "storage.h"
#include "portable.h"
#include "editor.h"
#include "utilities.h"
#endif

static BOOL Russian;
static NSString *L(NSString *en, NSString *ru) { return kr_mac_localize_string(en); }
static NSString *Decode(NSString *value) {char *decoded=kr_mac_decode_name(value.UTF8String);NSString *name=decoded?[NSString stringWithUTF8String:decoded]:value;free(decoded);return name?:value;}

/* GUI-only preference: explicit CLI/environment roots still take precedence.
 * A portable installation is the folder containing the .app, never Contents/MacOS
 * and never the process working directory chosen by Finder. */
static NSString *const rootPreference = @"MacPortableRoot";
static NSURL *usableRoot(NSURL *url) {
    NSURL *resolved = url.URLByResolvingSymlinksInPath;
    BOOL directory = NO;
    if (!resolved.isFileURL || ![NSFileManager.defaultManager fileExistsAtPath:resolved.path isDirectory:&directory] || !directory) return nil;
    for (NSString *part in resolved.path.pathComponents)
        if ([part hasSuffix:@".app"] || [part isEqualToString:@"AppTranslocation"]) return nil;
    return resolved;
}
static BOOL containsSessions(NSURL *url) {
    if (!url) return NO;
    char *path = kr_mac_directory_at(url.path.UTF8String, "Sessions");
    NSURL *sessions = path ? [NSURL fileURLWithPath:[NSString stringWithUTF8String:path]] : nil;
    free(path);
    if (!sessions) return NO;
    NSDirectoryEnumerator *entries = [NSFileManager.defaultManager enumeratorAtURL:sessions includingPropertiesForKeys:@[NSURLIsRegularFileKey, NSURLIsSymbolicLinkKey] options:NSDirectoryEnumerationSkipsHiddenFiles errorHandler:nil];
    for (NSURL *entry in entries) {
        NSNumber *regular = nil, *link = nil;
        [entry getResourceValue:&regular forKey:NSURLIsRegularFileKey error:nil];
        [entry getResourceValue:&link forKey:NSURLIsSymbolicLinkKey error:nil];
        if (link.boolValue) { [entries skipDescendants]; continue; }
        if (!regular.boolValue || [entry.path hasSuffix:@".tmp"]) continue;
        char *name = kr_mac_session_name_at(url.path.UTF8String, entry.lastPathComponent.UTF8String);
        BOOL found = name != NULL; free(name);
        if (found) return YES;
    }
    return NO;
}
static NSURL *initialRoot(NSURL *bundle, NSUserDefaults *preferences) {
    NSURL *besideApp = usableRoot(bundle.URLByDeletingLastPathComponent);
    if (containsSessions(besideApp)) return besideApp;
    NSString *saved = [preferences stringForKey:rootPreference];
    NSURL *remembered = saved.length ? usableRoot([NSURL fileURLWithPath:saved]) : nil;
    return remembered ?: besideApp;
}
static void restoreRoot(void) {
    if (getenv("KITTY_NG_PORTABLE_DIR")) return;
    if ([NSProcessInfo.processInfo.arguments containsObject:@"--portable-dir"]) return;
    NSURL *url = initialRoot(NSBundle.mainBundle.bundleURL, NSUserDefaults.standardUserDefaults);
    if (url) kr_mac_set_root(url.path.UTF8String);
}
int kr_mac_test_startup_ui(void) {
    NSString *base = [NSString stringWithUTF8String:kr_mac_root()];
    NSString *suite = [@"KiTTYNG.StartupTest." stringByAppendingString:NSUUID.UUID.UUIDString];
    NSUserDefaults *preferences = [[NSUserDefaults alloc] initWithSuiteName:suite];
    NSURL *install = [NSURL fileURLWithPath:[base stringByAppendingPathComponent:@"installation"]];
    NSURL *chosen = [NSURL fileURLWithPath:[base stringByAppendingPathComponent:@"chosen root"]];
    NSURL *bundle = [install URLByAppendingPathComponent:@"KiTTY-ng.app" isDirectory:YES];
    NSFileManager *fm = NSFileManager.defaultManager;
    [fm createDirectoryAtURL:bundle withIntermediateDirectories:YES attributes:nil error:nil];
    [fm createDirectoryAtURL:chosen withIntermediateDirectories:YES attributes:nil error:nil];
    BOOL ok = YES;
#define STARTUP_CHECK(condition) do { if (!(condition)) { fprintf(stderr, "FAIL startup: %s\n", #condition); ok = NO; } } while (0)
    STARTUP_CHECK([initialRoot(bundle, preferences).path isEqualToString:install.path]);
    [preferences setObject:chosen.path forKey:rootPreference]; [preferences synchronize];
    preferences = [[NSUserDefaults alloc] initWithSuiteName:suite];
    STARTUP_CHECK([initialRoot(bundle, preferences).path isEqualToString:chosen.path]);
    NSURL *sessions = [install URLByAppendingPathComponent:@"Sessions/folder"];
    [fm createDirectoryAtURL:sessions withIntermediateDirectories:YES attributes:nil error:nil];
    [@"HostName\\synthetic.invalid\\\n" writeToURL:[sessions URLByAppendingPathComponent:@"Example"] atomically:YES encoding:NSUTF8StringEncoding error:nil];
    STARTUP_CHECK([initialRoot(bundle, preferences).path isEqualToString:install.path]);
    STARTUP_CHECK(!usableRoot(bundle) && !usableRoot([bundle URLByAppendingPathComponent:@"Contents"]));
    [fm removeItemAtURL:[install URLByAppendingPathComponent:@"Sessions"] error:nil];
    [preferences setObject:bundle.path forKey:rootPreference];
    STARTUP_CHECK([initialRoot(bundle, preferences).path isEqualToString:install.path]);
    [preferences removePersistentDomainForName:suite];
    [fm removeItemAtURL:install error:nil]; [fm removeItemAtURL:chosen error:nil];
    if (ok) puts("PASS: .app parent discovery, existing nested sessions override remembered folder, persistent folder selection, bundle rejected");
    return ok ? 0 : 1;
#undef STARTUP_CHECK
}

@interface SessionTableView : NSTableView
@end
@implementation SessionTableView
- (void)keyDown:(NSEvent *)event {
    if ((event.keyCode == 36 || event.keyCode == 76) &&
        !(event.modifierFlags & (NSEventModifierFlagCommand |
                                 NSEventModifierFlagControl |
                                 NSEventModifierFlagOption))) {
        [NSApp sendAction:self.doubleAction to:self.target from:self];
        return;
    }
    [super keyDown:event];
}
@end

@interface PortableBrowser : NSObject <NSApplicationDelegate, NSTableViewDataSource, NSTableViewDelegate>
@property(nonatomic,strong) NSPopUpButton *languagePicker;
@property(nonatomic,strong) NSWindow *window;
@property(nonatomic,strong) NSTableView *table;
@property(nonatomic,strong) NSTextField *pathLabel;
@property(nonatomic,strong) NSTextField *detail;
@property(nonatomic,strong) NSSearchField *search;
@property(nonatomic,strong) NSURL *root;
@property(nonatomic,strong) NSURL *folder;
@property(nonatomic,strong) NSMutableArray<NSDictionary *> *rows;
@end
@implementation PortableBrowser
- (void)applicationDidFinishLaunching:(NSNotification *)notification {
    restoreRoot();
    Russian = kr_mac_language()==1;
    kr_mac_install_status_menu();
    NSMenu *menu = [NSMenu new]; NSMenuItem *appItem = [NSMenuItem new]; [menu addItem:appItem];
    NSMenu *appMenu = [[NSMenu alloc] initWithTitle:@"KiTTY-ng"]; appItem.submenu = appMenu;
    NSMenuItem *about = [[NSMenuItem alloc] initWithTitle:L(@"About KiTTY-ng", @"О программе KiTTY-ng") action:@selector(about:) keyEquivalent:@""]; about.target = self; [appMenu addItem:about];
    NSMenuItem *keygen=[[NSMenuItem alloc]initWithTitle:@"KiTTYgen" action:@selector(keygen:) keyEquivalent:@""];keygen.target=self;[appMenu addItem:keygen];NSMenuItem *agent=[[NSMenuItem alloc]initWithTitle:@"Kageant" action:@selector(agent:) keyEquivalent:@""];agent.target=self;[appMenu addItem:agent];NSMenuItem *authorities=[[NSMenuItem alloc]initWithTitle:L(@"SSH authorities…",@"Центры сертификации SSH…") action:@selector(authorities:) keyEquivalent:@""];authorities.target=self;[appMenu addItem:authorities];
    [appMenu addItem:[NSMenuItem separatorItem]];
    [appMenu addItemWithTitle:L(@"Quit KiTTY-ng", @"Завершить KiTTY-ng") action:@selector(terminate:) keyEquivalent:@"q"]; kr_mac_add_edit_menu(menu);
    kr_mac_add_session_menu(menu);NSApp.mainMenu = menu;
    self.window = [[NSWindow alloc] initWithContentRect:NSMakeRect(0,0,820,600) styleMask:NSWindowStyleMaskTitled|NSWindowStyleMaskClosable|NSWindowStyleMaskMiniaturizable|NSWindowStyleMaskResizable backing:NSBackingStoreBuffered defer:NO];
    self.window.releasedWhenClosed = NO; self.window.title = @"KiTTY-ng"; self.window.minSize = NSMakeSize(680,420);
    NSView *view = self.window.contentView;
    NSTextField *notice = [NSTextField wrappingLabelWithString:L(@"macOS preview: portable session browser. SSH terminal is under development.", @"Предварительная версия macOS: просмотр портативных сеансов. SSH-терминал в разработке.")];
#ifdef KITTY_NG_CLIENT
    notice.stringValue = L(@"Portable sessions shared with the Windows version.", @"Портативные сеансы, общие с Windows-версией.");
    NSTimer *timer = [NSTimer timerWithTimeInterval:0.01 repeats:YES block:^(NSTimer *t) { kr_mac_pump(); }];
    [NSRunLoop.mainRunLoop addTimer:timer forMode:NSRunLoopCommonModes];
#endif
    self.languagePicker=[[NSPopUpButton alloc]initWithFrame:NSMakeRect(515,554,150,28) pullsDown:NO];[self.languagePicker addItemsWithTitles:@[@"English",@"Русский"]];[self.languagePicker selectItemAtIndex:Russian?1:0];self.languagePicker.autoresizingMask=NSViewMinXMargin|NSViewMinYMargin;[view addSubview:self.languagePicker];
    NSButton *apply=[NSButton buttonWithTitle:L(@"Apply",@"Применить") target:self action:@selector(applyLanguage:)];apply.frame=NSMakeRect(678,553,122,30);apply.autoresizingMask=NSViewMinXMargin|NSViewMinYMargin;[view addSubview:apply];
    notice.frame = NSMakeRect(20,550,480,35); notice.autoresizingMask = NSViewWidthSizable|NSViewMinYMargin; [view addSubview:notice];
    self.pathLabel = [NSTextField labelWithString:@""]; self.pathLabel.frame = NSMakeRect(20,510,570,28); self.pathLabel.lineBreakMode = NSLineBreakByTruncatingMiddle; self.pathLabel.autoresizingMask = NSViewWidthSizable|NSViewMinYMargin; [view addSubview:self.pathLabel];
    NSButton *choose = [NSButton buttonWithTitle:L(@"Choose folder…", @"Выбрать каталог…") target:self action:@selector(chooseFolder:)]; choose.frame = NSMakeRect(620,508,180,30); choose.autoresizingMask = NSViewMinXMargin|NSViewMinYMargin; [view addSubview:choose];
    self.search = [[NSSearchField alloc] initWithFrame:NSMakeRect(20,466,780,30)]; self.search.placeholderString = L(@"Quick search", @"Быстрый поиск"); self.search.target = self; self.search.action = @selector(searchChanged:); self.search.sendsSearchStringImmediately = YES; self.search.autoresizingMask = NSViewWidthSizable|NSViewMinYMargin; [view addSubview:self.search];
    NSScrollView *scroll = [[NSScrollView alloc] initWithFrame:NSMakeRect(20,120,780,332)]; scroll.hasVerticalScroller = YES; scroll.borderType = NSBezelBorder; scroll.autoresizingMask = NSViewWidthSizable|NSViewHeightSizable;
    self.table = [[SessionTableView alloc] initWithFrame:scroll.bounds]; self.table.dataSource = self; self.table.delegate = self; self.table.target = self; self.table.doubleAction = @selector(enter:);
    NSTableColumn *column = [[NSTableColumn alloc] initWithIdentifier:@"name"]; column.title = L(@"Saved sessions", @"Сохранённые сеансы"); column.width = 750; column.editable = NO; [self.table addTableColumn:column]; self.table.columnAutoresizingStyle = NSTableViewUniformColumnAutoresizingStyle; scroll.documentView = self.table; [view addSubview:scroll];
    self.detail = [NSTextField wrappingLabelWithString:@""]; self.detail.frame = NSMakeRect(20,16,610,52); self.detail.autoresizingMask = NSViewWidthSizable; [view addSubview:self.detail];
#ifdef KITTY_NG_CLIENT
    NSButton *open = [NSButton buttonWithTitle:L(@"Open", @"Открыть") target:self action:@selector(openSession:)]; open.frame=NSMakeRect(650,40,150,34); open.autoresizingMask=NSViewMinXMargin; [view addSubview:open];
#endif
    NSArray *actions=@[@[L(@"Quick connect",@"Быстрое подключение"),NSStringFromSelector(@selector(quickConnect:))],@[L(@"New",@"Новый"),NSStringFromSelector(@selector(newProfile:))],@[L(@"Edit",@"Изменить"),NSStringFromSelector(@selector(editProfile:))],@[L(@"Delete",@"Удалить"),NSStringFromSelector(@selector(deleteProfile:))],@[L(@"Duplicate",@"Дублировать"),NSStringFromSelector(@selector(duplicateProfile:))],@[L(@"Refresh",@"Обновить"),NSStringFromSelector(@selector(profilesChanged:))],@[L(@"Upgrade profiles",@"Обновить профили"),NSStringFromSelector(@selector(upgradeProfiles:))]];
    NSStackView *actionBar=[NSStackView new];actionBar.orientation=NSUserInterfaceLayoutOrientationHorizontal;actionBar.spacing=8;actionBar.distribution=NSStackViewDistributionFillProportionally;actionBar.frame=NSMakeRect(20,82,980,28);actionBar.autoresizingMask=NSViewWidthSizable;[view addSubview:actionBar];
    for(NSArray *action in actions){NSButton *button=[NSButton buttonWithTitle:action[0] target:self action:NSSelectorFromString(action[1])];[actionBar addArrangedSubview:button];}
    [self.window setContentSize:NSMakeSize(1020,600)];self.window.minSize=NSMakeSize(1020,420);actionBar.frame=NSMakeRect(20,82,980,28);
    [NSNotificationCenter.defaultCenter addObserver:self selector:@selector(profilesChanged:) name:@"KiTTYNGProfilesChanged" object:nil];
    [NSNotificationCenter.defaultCenter addObserver:self selector:@selector(languageChanged:) name:@"KiTTYNGLanguageChanged" object:nil];
    self.root = kr_mac_root()?[NSURL fileURLWithPath:[NSString stringWithUTF8String:kr_mac_root()] isDirectory:YES]:NSBundle.mainBundle.bundleURL.URLByDeletingLastPathComponent;
    self.folder = [self sessionsDirectory];
    [self.window center]; [self.window makeKeyAndOrderFront:nil]; [NSApp activateIgnoringOtherApps:YES];
    if ([self.root.path containsString:@"/AppTranslocation/"] && !containsSessions(self.root)) [self chooseFolder:nil]; else [self reload];
}
- (void)quickConnect:(id)sender{kr_mac_quick_connect();}
- (NSURL *)sessionsDirectory {
#ifdef KITTY_NG_CLIENT
    char *path=kr_mac_directory_at(self.root.path.UTF8String,"Sessions");
    NSURL *result=path?[NSURL fileURLWithPath:[NSString stringWithUTF8String:path] isDirectory:YES]:nil;free(path);return result;
#else
    return [self.root URLByAppendingPathComponent:@"Sessions" isDirectory:YES];
#endif
}
- (void)reload {
    self.pathLabel.stringValue = self.root.path ?: @""; self.rows = [NSMutableArray new];
    if (self.folder && ![self.folder.path isEqualToString:[self sessionsDirectory].path]) [self.rows addObject:@{@"name":@"..",@"url":self.folder.URLByDeletingLastPathComponent,@"directory":@YES}];
    NSArray<NSURL *> *entries = self.folder ? [NSFileManager.defaultManager contentsOfDirectoryAtURL:self.folder includingPropertiesForKeys:@[NSURLIsDirectoryKey,NSURLIsSymbolicLinkKey] options:NSDirectoryEnumerationSkipsHiddenFiles error:nil] : nil;
    NSString *query = self.search.stringValue;
    for (NSURL *url in [entries sortedArrayUsingComparator:^NSComparisonResult(NSURL *a, NSURL *b) { NSNumber *ad=nil,*bd=nil;[a getResourceValue:&ad forKey:NSURLIsDirectoryKey error:nil];[b getResourceValue:&bd forKey:NSURLIsDirectoryKey error:nil];if(ad.boolValue!=bd.boolValue)return ad.boolValue?NSOrderedAscending:NSOrderedDescending;return [Decode(a.lastPathComponent) localizedStandardCompare:Decode(b.lastPathComponent)]; }]) {
        NSNumber *directory, *link; [url getResourceValue:&directory forKey:NSURLIsDirectoryKey error:nil]; [url getResourceValue:&link forKey:NSURLIsSymbolicLinkKey error:nil];
        if (link.boolValue || [url.lastPathComponent hasSuffix:@".tmp"]) continue;
        NSString *name = Decode(url.lastPathComponent);
#ifdef KITTY_NG_CLIENT
        if(!directory.boolValue){char *logical=kr_mac_session_name_at(self.root.path.UTF8String,url.lastPathComponent.UTF8String);if(!logical)continue;name=[NSString stringWithUTF8String:logical];free(logical);if(!name)continue;}
#endif
        if (query.length && [name rangeOfString:query options:NSCaseInsensitiveSearch|NSDiacriticInsensitiveSearch].location == NSNotFound) continue;
        [self.rows addObject:@{@"name":directory.boolValue ? [NSString stringWithFormat:@"<%@>",name] : name,@"url":url,@"directory":directory ?: @NO}];
    }
    [self.table reloadData]; self.detail.stringValue = entries ? L(@"Double-click a folder to enter it. Profiles are read without changes.", @"Двойной щелчок открывает папку. Профили читаются без изменений.") : L(@"No Sessions directory found. Choose the common portable folder containing Sessions and kitty.ini.", @"Каталог Sessions не найден. Выберите общую портативную папку с Sessions и kitty.ini.");
}
- (NSInteger)numberOfRowsInTableView:(NSTableView *)tableView { return self.rows.count; }
- (id)tableView:(NSTableView *)tableView objectValueForTableColumn:(NSTableColumn *)column row:(NSInteger)row { return self.rows[row][@"name"]; }
- (void)tableViewSelectionDidChange:(NSNotification *)notification {
    NSInteger row = self.table.selectedRow; if (row < 0 || row >= self.rows.count || [self.rows[row][@"directory"] boolValue]) return;
    NSURL *url = self.rows[row][@"url"]; NSData *data = [NSData dataWithContentsOfURL:url];
    NSString *text = [[NSString alloc] initWithData:data encoding:NSUTF8StringEncoding];
    if (!text) { self.detail.stringValue = L(@"The profile could not be decoded. Its original file is unchanged.", @"Профиль не удалось декодировать. Исходный файл не изменён."); return; }
    NSMutableDictionary *values = [NSMutableDictionary new];
    for (NSString *line in [text componentsSeparatedByCharactersInSet:NSCharacterSet.newlineCharacterSet]) {
        NSRange slash = [line rangeOfString:@"\\"];
        if (slash.location == NSNotFound || ![line hasSuffix:@"\\"] || line.length <= slash.location+1) continue;
        NSString *key = [line substringToIndex:slash.location];
        if ([@[@"HostName",@"UserName",@"PortNumber",@"Protocol",@"Comment"] containsObject:key]) values[key] = Decode([line substringWithRange:NSMakeRange(slash.location+1,line.length-slash.location-2)]);
    }
    self.detail.stringValue = [NSString stringWithFormat:@"%@  %@:%@\n%@", values[@"UserName"] ?: @"",values[@"HostName"] ?: @"",values[@"PortNumber"] ?: @"22",values[@"Comment"] ?: @""];
}
- (void)enter:(id)sender { NSInteger row = self.table.selectedRow; if (row >= 0 && row < self.rows.count && [self.rows[row][@"directory"] boolValue]) { self.folder = self.rows[row][@"url"]; self.search.stringValue = @""; [self reload]; } else if(row>=0) [self openSession:nil]; }
- (NSString *)selectedSession {
    NSInteger row=self.table.selectedRow;if(row<0||row>=self.rows.count||[self.rows[row][@"directory"]boolValue])return nil;
    NSURL *url=self.rows[row][@"url"];NSString *prefix=[[self sessionsDirectory].path stringByAppendingString:@"/"];if(![url.path hasPrefix:prefix])return nil;
    NSString *name=[url.path substringFromIndex:prefix.length];NSMutableArray *parts=[NSMutableArray new];NSArray *components=[name componentsSeparatedByString:@"/"];
    for(NSUInteger i=0;i<components.count;i++){NSString *part=components[i];if(i+1==components.count){char *logical=kr_mac_session_name_at(self.root.path.UTF8String,part.UTF8String);if(!logical)return nil;NSString *decoded=[NSString stringWithUTF8String:logical];free(logical);if(!decoded)return nil;[parts addObject:decoded];}else[parts addObject:Decode(part)];}return [parts componentsJoinedByString:@"/"];
}
- (void)openSession:(id)sender {NSString *name=[self selectedSession];if(name)kr_mac_open_session(name.UTF8String,self.root.path.UTF8String);}
- (void)editProfile:(id)sender {NSString *name=[self selectedSession];if(name&&kr_mac_select_root(self.root.path.UTF8String))kr_mac_edit_profile(name.UTF8String,true);}
- (void)newProfile:(id)sender {
    if(!kr_mac_select_root(self.root.path.UTF8String))return;NSString *prefix=[[self sessionsDirectory].path stringByAppendingString:@"/"],*folder=self.folder.path;
    NSMutableArray *parts=[NSMutableArray new];if([folder hasPrefix:prefix])for(NSString *part in [[folder substringFromIndex:prefix.length]componentsSeparatedByString:@"/"])[parts addObject:Decode(part)];
    [parts addObject:L(@"New session",@"Новый сеанс")];kr_mac_edit_profile([parts componentsJoinedByString:@"/"].UTF8String,false);
}
- (void)duplicateProfile:(id)sender {
    NSString *source=[self selectedSession];if(!source||!kr_mac_select_root(self.root.path.UTF8String))return;
    NSAlert *alert=[NSAlert new];alert.messageText=L(@"Duplicate session",@"Дублировать сеанс");alert.informativeText=L(@"Enter a new profile name, then review its settings.",@"Введите имя нового профиля, затем проверьте его настройки.");
    NSTextField *name=[[NSTextField alloc]initWithFrame:NSMakeRect(0,0,420,28)];name.stringValue=[source stringByAppendingString:L(@" copy",@" копия")];alert.accessoryView=name;
    [alert addButtonWithTitle:L(@"Cancel",@"Отмена")];[alert addButtonWithTitle:L(@"Continue",@"Продолжить")];
    if([alert runModal]!=NSAlertSecondButtonReturn)return;
    NSString *target=[name.stringValue stringByTrimmingCharactersInSet:NSCharacterSet.whitespaceAndNewlineCharacterSet];char *path=kr_mac_profile_path(target.UTF8String);
    BOOL exists=path&&[NSFileManager.defaultManager fileExistsAtPath:[NSString stringWithUTF8String:path]];free(path);
    if(!target.length||[target isEqualToString:source]||exists){NSAlert *error=[NSAlert new];error.messageText=L(@"Session already exists",@"Сеанс уже существует");[error runModal];return;}
    kr_mac_duplicate_profile(source.UTF8String,target.UTF8String);
}
- (void)deleteProfile:(id)sender {NSString *name=[self selectedSession];if(!name||!kr_mac_select_root(self.root.path.UTF8String))return;NSAlert *alert=[NSAlert new];alert.messageText=L(@"Delete session?",@"Удалить сеанс?");alert.informativeText=name;[alert addButtonWithTitle:L(@"Cancel",@"Отмена")];[alert addButtonWithTitle:L(@"Delete",@"Удалить")];if([alert runModal]==NSAlertSecondButtonReturn){del_settings(name.UTF8String);[self reload];}}
- (void)profilesChanged:(id)sender {[self reload];}
- (void)upgradeProfiles:(id)sender {if(!kr_mac_select_root(self.root.path.UTF8String))return;int count=kr_mac_upgrade_profiles();NSAlert *alert=[NSAlert new];alert.messageText=L(@"Profiles updated",@"Профили обновлены");alert.informativeText=[NSString stringWithFormat:L(@"Converted portable file links in %d profiles.",@"Переносимые ссылки преобразованы в %d профилях."),count];[alert runModal];[self reload];}
- (void)applyLanguage:(id)sender {
    if(!kr_mac_set_language((int)self.languagePicker.indexOfSelectedItem)){NSAlert *alert=[NSAlert new];alert.messageText=@"KiTTY-ng";alert.informativeText=L(@"Unable to save language",@"Не удалось сохранить язык");[alert runModal];return;}
    [NSNotificationCenter.defaultCenter postNotificationName:@"KiTTYNGLanguageChanged" object:nil];

}
- (void)languageChanged:(NSNotification*)note{Russian=kr_mac_language()==1;[self.languagePicker selectItemAtIndex:kr_mac_language()];kr_mac_localize_view(self.window.contentView);kr_mac_localize_menu(NSApp.mainMenu);self.search.placeholderString=L(@"Quick search",@"Быстрый поиск");[self reload];}
- (void)keygen:(id)sender{kr_mac_show_keygen();}
- (void)agent:(id)sender{kr_mac_show_agent();}
- (void)authorities:(id)sender{kr_mac_show_ca();}
- (void)searchChanged:(id)sender { [self reload]; }
- (void)chooseFolder:(id)sender {
    NSOpenPanel *panel = [NSOpenPanel openPanel]; panel.canChooseFiles = NO; panel.canChooseDirectories = YES; panel.allowsMultipleSelection = NO; panel.message = L(@"Choose the shared KiTTY portable directory", @"Выберите общий портативный каталог KiTTY");
    if ([panel runModal] == NSModalResponseOK && kr_mac_select_root(panel.URL.path.UTF8String)) { self.root = [NSURL fileURLWithPath:[NSString stringWithUTF8String:kr_mac_root()]]; [NSUserDefaults.standardUserDefaults setObject:self.root.path forKey:rootPreference]; self.folder = [self sessionsDirectory]; [self reload]; }
}
- (void)about:(id)sender { NSAlert *alert = [NSAlert new]; alert.messageText = @TEXTVER; alert.informativeText = L(@"Native macOS Universal 2. Based on PuTTY and KiTTY.\n© nr084 — " @__DATE__, @"Нативная версия macOS Universal 2. На базе PuTTY и KiTTY.\n© nr084 — " @__DATE__); [alert runModal]; }
- (NSApplicationTerminateReply)applicationShouldTerminate:(NSApplication *)sender { return kr_mac_request_termination() ? NSTerminateNow : NSTerminateCancel; }
- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication *)sender { return NO; }
@end
static int askpass(const char *mode,const char *question){
    NSApplication *app=NSApplication.sharedApplication;app.activationPolicy=NSApplicationActivationPolicyAccessory;
    /* This entry point runs a modal loop instead of -run, so complete launch
     * explicitly before activating the standalone prompt. */
    [app finishLaunching];[app activateIgnoringOtherApps:YES];
    NSAlert *alert=[NSAlert new];alert.messageText=@"KiTTY-ng — SSH";alert.informativeText=[NSString stringWithUTF8String:question]?:@"";
    BOOL ru=kr_mac_language()==1;NSString *reply=nil;
    if(!strcmp(mode,"hostkey")){
        [alert addButtonWithTitle:ru?@"Отмена":@"Cancel"];[alert addButtonWithTitle:ru?@"Принять и сохранить":@"Accept and save"];[alert addButtonWithTitle:ru?@"Подключиться один раз":@"Connect once"];
        NSInteger result=[alert runModal];if(result==NSAlertSecondButtonReturn)reply=@"save";else if(result==NSAlertThirdButtonReturn)reply=@"once";
    }else if(!strcmp(mode,"confirm")){
        [alert addButtonWithTitle:ru?@"Отмена":@"Cancel"];[alert addButtonWithTitle:ru?@"Продолжить":@"Continue"];if([alert runModal]==NSAlertSecondButtonReturn)reply=@"yes";
    }else if(!strcmp(mode,"secret")||!strcmp(mode,"text")){
        NSTextField *field=!strcmp(mode,"secret")?[[NSSecureTextField alloc]initWithFrame:NSMakeRect(0,0,380,28)]:[[NSTextField alloc]initWithFrame:NSMakeRect(0,0,380,28)];alert.accessoryView=field;
        [alert addButtonWithTitle:ru?@"Продолжить":@"Continue"];[alert addButtonWithTitle:ru?@"Отмена":@"Cancel"];[alert.window makeFirstResponder:field];if([alert runModal]==NSAlertFirstButtonReturn)reply=field.stringValue;field.stringValue=@"";
    }
    if(!reply)return 1;NSData *data=[reply dataUsingEncoding:NSUTF8StringEncoding];return fwrite(data.bytes,1,data.length,stdout)==data.length&&fflush(stdout)==0?0:1;
}
extern int kr_mac_self_test(const char *file);
extern int kr_mac_self_test_ui(void);
int main(int argc,const char **argv){@autoreleasepool{
    settings_set_default_protocol(PROT_SSH);settings_set_default_port(22);
    if(argc==2&&!strcmp(argv[1],"--self-test-ui"))return kr_mac_self_test_ui();
    if(argc==3&&!strcmp(argv[1],"--self-test-portable"))return kr_mac_self_test(argv[2]);
    if(argc==3&&!strcmp(argv[1],"--agent-watch")){char *end;long pid=strtol(argv[2],&end,10);if(*end||pid<=1)return 1;int q=kqueue();if(q<0)return 1;struct kevent event;EV_SET(&event,pid,EVFILT_PROC,EV_ADD|EV_ONESHOT,NOTE_EXIT,0,NULL);if(kevent(q,&event,1,NULL,0,NULL)<0){close(q);return 0;}int status=kevent(q,NULL,0,&event,1,NULL);close(q);return status<0?1:0;}
    if(argc==4&&!strcmp(argv[1],"--askpass")){NSApplication.sharedApplication.mainMenu=[NSMenu new];kr_mac_add_edit_menu(NSApp.mainMenu);return askpass(argv[2],argv[3]);}
    NSApplication *app=NSApplication.sharedApplication;app.activationPolicy=NSApplicationActivationPolicyRegular;
    __attribute__((objc_precise_lifetime)) PortableBrowser *delegate=[PortableBrowser new];app.delegate=delegate;[app run];
}return 0;}
