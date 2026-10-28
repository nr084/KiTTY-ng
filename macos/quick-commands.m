/* Native editor for the shared, profile-independent portable command list. */
#import <AppKit/AppKit.h>
#include "putty.h"
#include "portable.h"
#include "language.h"
#include "kittyng-quick-commands.h"
#include "quick-commands.h"

static NSString *Q(NSString *source) { return kr_mac_localize_string(source); }
static NSString *U(const char *text) { return text ? [NSString stringWithUTF8String:text] : @""; }
static void quickError(const char *message) {
    NSAlert *alert=[NSAlert new];alert.messageText=Q(@"Quick commands");
    alert.informativeText=Q(U(message));[alert runModal];
}
void kr_mac_initialize_quick_commands(void) {
    KrQuickCommands commands;char *error=NULL;
    kr_quick_load(&commands,Q(@"Update Ubuntu").UTF8String,&error);
    kr_quick_free(&commands);sfree(error);
}

@interface MacQuickEditor : NSObject <NSWindowDelegate,NSTableViewDataSource,NSTableViewDelegate> {
@public KrQuickCommands commands;
}
@property(nonatomic,strong) NSWindow *window;
@property(nonatomic,strong) NSTableView *table;
@property(nonatomic,strong) NSTextField *nameField,*commandField;
@property(nonatomic,strong) NSButton *enterButton,*addButton,*deleteButton;
@property(nonatomic,strong) NSMutableArray<NSNumber *> *slots;
@property(nonatomic,strong) NSString *root;
@property NSInteger selected;
@property BOOL refreshing;
- (void)reload;
- (void)add:(id)sender;
- (void)save:(id)sender;
- (void)remove:(id)sender;
@end
static MacQuickEditor *quickEditor;
@implementation MacQuickEditor
- (void)dealloc { kr_quick_free(&commands); }
- (void)windowWillClose:(NSNotification *)note {
    [NSNotificationCenter.defaultCenter removeObserver:self];self.window.delegate=nil;
    if(quickEditor==self)quickEditor=nil;
}
- (void)rootChanged:(NSNotification *)note {
    if(![self.root isEqualToString:U(kr_mac_root())])[self.window close];
}
- (void)languageChanged:(NSNotification *)note {
    self.window.title=Q(@"Quick commands");kr_mac_localize_view(self.window.contentView);
    [self.table reloadData];
}
- (NSInteger)numberOfRowsInTableView:(NSTableView *)table { return self.slots.count; }
- (id)tableView:(NSTableView *)table objectValueForTableColumn:(NSTableColumn *)column row:(NSInteger)row {
    return U(commands.items[self.slots[row].intValue].name);
}
- (void)fields {
    KrQuickCommand *entry=self.selected>=0?&commands.items[self.selected]:NULL;
    self.nameField.stringValue=entry?U(entry->name):@"";
    self.commandField.stringValue=entry?U(entry->instruction):@"";
    self.enterButton.state=entry&&entry->name?!entry->enter?NSControlStateValueOff:NSControlStateValueOn:NSControlStateValueOn;
    self.deleteButton.enabled=entry&&entry->name;
}
- (void)tableViewSelectionDidChange:(NSNotification *)note {
    if(self.refreshing)return;
    NSInteger row=self.table.selectedRow;
    self.selected=row>=0&&row<self.slots.count?self.slots[row].intValue:-1;[self fields];
}
- (void)reload {
    self.refreshing=YES;kr_quick_free(&commands);char *error=NULL;
    BOOL ok=kr_quick_load(&commands,Q(@"Update Ubuntu").UTF8String,&error);
    self.slots=[NSMutableArray new];
    for(int i=0;i<KR_QUICK_LIMIT;i++)if(commands.items[i].name)[self.slots addObject:@(i)];
    [self.table reloadData];NSInteger row=[self.slots indexOfObject:@(self.selected)];
    if(row==NSNotFound&&self.slots.count){row=0;self.selected=self.slots[0].intValue;}
    if(!self.slots.count)self.selected=-1;
    if(row!=NSNotFound&&self.slots.count)[self.table selectRowIndexes:[NSIndexSet indexSetWithIndex:row] byExtendingSelection:NO];
    self.addButton.enabled=self.slots.count<KR_QUICK_LIMIT;[self fields];self.refreshing=NO;
    if(!ok)quickError(error);sfree(error);
}
- (void)add:(id)sender {
    int slot=0;while(slot<KR_QUICK_LIMIT&&commands.items[slot].name)slot++;
    if(slot==KR_QUICK_LIMIT){quickError("Up to 20 quick commands can be saved.");return;}
    self.refreshing=YES;[self.table deselectAll:nil];self.refreshing=NO;
    self.selected=slot;[self fields];[self.window makeFirstResponder:self.nameField];
}
- (BOOL)writeName:(const char *)name command:(const char *)text enter:(BOOL)enter {
    if(![self.root isEqualToString:U(kr_mac_root())]){[self.window close];return NO;}
    char *error=NULL;BOOL ok=kr_quick_update((int)self.selected,name,text,enter,&error);
    if(ok)[self reload];else quickError(error);sfree(error);return ok;
}
- (void)save:(id)sender {
    if(self.selected<0)[self add:nil];if(self.selected<0)return;
    [self writeName:self.nameField.stringValue.UTF8String command:self.commandField.stringValue.UTF8String enter:self.enterButton.state==NSControlStateValueOn];
}
- (void)remove:(id)sender {
    if(self.selected>=0&&commands.items[self.selected].name)[self writeName:NULL command:NULL enter:NO];
}
@end

static NSTextField *label(NSView *view,NSString *key,NSRect frame) {
    NSTextField *field=[NSTextField wrappingLabelWithString:Q(key)];field.frame=frame;
    field.autoresizingMask=NSViewWidthSizable|NSViewMinYMargin;[view addSubview:field];return field;
}
static NSButton *button(NSView *view,NSString *key,id target,SEL action,NSRect frame,NSString *identifier) {
    NSButton *button=[NSButton buttonWithTitle:Q(key) target:target action:action];button.frame=frame;
    button.identifier=identifier;[view addSubview:button];return button;
}
static MacQuickEditor *createEditor(void) {
    MacQuickEditor *editor=[MacQuickEditor new];editor.selected=-1;editor.root=U(kr_mac_root());
    editor.window=[[NSWindow alloc]initWithContentRect:NSMakeRect(0,0,940,500) styleMask:NSWindowStyleMaskTitled|NSWindowStyleMaskClosable|NSWindowStyleMaskResizable backing:NSBackingStoreBuffered defer:NO];
    editor.window.title=Q(@"Quick commands");editor.window.minSize=NSMakeSize(940,500);editor.window.releasedWhenClosed=NO;editor.window.delegate=editor;
    NSView *view=editor.window.contentView;
    label(view,@"Saved commands (up to 20):",NSMakeRect(20,452,280,25));
    NSScrollView *scroll=[[NSScrollView alloc]initWithFrame:NSMakeRect(20,150,280,295)];scroll.hasVerticalScroller=YES;scroll.borderType=NSBezelBorder;scroll.autoresizingMask=NSViewHeightSizable;
    editor.table=[[NSTableView alloc]initWithFrame:scroll.bounds];editor.table.identifier=@"KiTTYNG.QuickCommands.List";
    NSTableColumn *column=[[NSTableColumn alloc]initWithIdentifier:@"name"];column.title=Q(@"Command name:");column.width=260;column.editable=NO;
    [editor.table addTableColumn:column];editor.table.headerView=nil;editor.table.delegate=editor;editor.table.dataSource=editor;scroll.documentView=editor.table;[view addSubview:scroll];
    label(view,@"Command name:",NSMakeRect(330,422,590,25));
    editor.nameField=[[NSTextField alloc]initWithFrame:NSMakeRect(330,386,590,30)];editor.nameField.identifier=@"KiTTYNG.QuickCommands.Name";
    editor.nameField.autoresizingMask=NSViewWidthSizable|NSViewMinYMargin;[view addSubview:editor.nameField];
    label(view,@"Command to send:",NSMakeRect(330,336,590,25));
    editor.commandField=[[NSTextField alloc]initWithFrame:NSMakeRect(330,299,590,30)];editor.commandField.identifier=@"KiTTYNG.QuickCommands.Instruction";
    editor.commandField.font=[NSFont monospacedSystemFontOfSize:12 weight:NSFontWeightRegular];
    editor.commandField.cell.wraps=NO;editor.commandField.cell.scrollable=YES;editor.commandField.cell.usesSingleLineMode=YES;
    editor.commandField.autoresizingMask=NSViewWidthSizable|NSViewMinYMargin;[view addSubview:editor.commandField];
    editor.enterButton=[NSButton checkboxWithTitle:Q(@"Press Enter after sending the command") target:nil action:nil];
    editor.enterButton.frame=NSMakeRect(330,246,590,30);editor.enterButton.autoresizingMask=NSViewWidthSizable|NSViewMinYMargin;[view addSubview:editor.enterButton];
    label(view,@"Save and Delete apply immediately to all sessions. Commands are stored outside session profiles.",NSMakeRect(20,70,900,55)).autoresizingMask=NSViewWidthSizable;
    editor.addButton=button(view,@"Add",editor,@selector(add:),NSMakeRect(20,20,110,32),@"KiTTYNG.QuickCommands.Add");
    editor.deleteButton=button(view,@"Delete",editor,@selector(remove:),NSMakeRect(140,20,130,32),@"KiTTYNG.QuickCommands.Delete");
    button(view,@"Save",editor,@selector(save:),NSMakeRect(740,20,180,32),@"KiTTYNG.QuickCommands.Save").autoresizingMask=NSViewMinXMargin;
    [NSNotificationCenter.defaultCenter addObserver:editor selector:@selector(languageChanged:) name:@"KiTTYNGLanguageChanged" object:nil];
    [NSNotificationCenter.defaultCenter addObserver:editor selector:@selector(rootChanged:) name:@"KiTTYNGPortableRootChanged" object:nil];
    [editor reload];[editor.window center];return editor;
}
void kr_mac_show_quick_commands(id sender) {
    if(quickEditor&&![quickEditor.root isEqualToString:U(kr_mac_root())])[quickEditor.window close];
    if(!quickEditor)quickEditor=createEditor();[quickEditor.window makeKeyAndOrderFront:nil];
}
@interface QuickSettingsTarget : NSObject
- (void)show:(id)sender;
@end
@implementation QuickSettingsTarget
- (void)show:(id)sender { kr_mac_show_quick_commands(sender); }
@end
void kr_mac_add_quick_settings_menu(NSMenu *menu) {
    static QuickSettingsTarget *target;if(!target)target=[QuickSettingsTarget new];
    NSMenuItem *window=[[NSMenuItem alloc]initWithTitle:Q(@"Window") action:nil keyEquivalent:@""];
    window.submenu=[[NSMenu alloc]initWithTitle:Q(@"Window")];
    NSMenuItem *settings=[[NSMenuItem alloc]initWithTitle:Q(@"Quick commands") action:@selector(show:) keyEquivalent:@""];settings.target=target;
    [window.submenu addItem:settings];[menu addItem:window];
}

int kr_mac_test_quick_commands_ui(void) {
    NSString *original=U(kr_mac_root());NSString *path=[original stringByAppendingPathComponent:@"native-quick-commands"];
    NSFileManager *fm=NSFileManager.defaultManager;[fm createDirectoryAtPath:path withIntermediateDirectories:YES attributes:nil error:nil];
    /* A BOM-encoded shared INI with unrelated Unicode settings. */
    NSString *ini=[path stringByAppendingPathComponent:@"kitty.ini"];
    [@"[KiTTY]\nSessionTabs=0\n[Other]\nvalue=Кириллица 😀\n" writeToFile:ini atomically:YES encoding:NSUTF16StringEncoding error:nil];
    kr_mac_set_root(path.UTF8String);MacQuickEditor *editor=createEditor();BOOL ok=YES;NSString *longText=nil;
#define QUICK_CHECK(condition) do{if(!(condition)){fprintf(stderr,"FAIL quick commands: %s\n",#condition);ok=NO;goto finish;}}while(0)
    QUICK_CHECK(editor.slots.count==1&&editor->commands.items[0].enter);
    QUICK_CHECK(!strcmp(editor->commands.items[0].instruction,"apt -y update && apt -y upgrade && apt -y autoremove"));
    editor.nameField.stringValue=@"Сервер & тест 😀";editor.commandField.stringValue=@"printf 'Привет = мир' && echo ok";editor.enterButton.state=NSControlStateValueOff;[editor save:nil];
    QUICK_CHECK(!strcmp(editor->commands.items[0].name,"Сервер & тест 😀")&&!editor->commands.items[0].enter);
    longText=[@"echo " stringByPaddingToLength:60000 withString:@"x" startingAtIndex:0];
    for(int i=1;i<KR_QUICK_LIMIT;i++){[editor add:nil];editor.nameField.stringValue=[NSString stringWithFormat:@"Command & %d",i];editor.commandField.stringValue=i==19?longText:@"echo safe";[editor save:nil];}
    QUICK_CHECK(editor.slots.count==20&&!editor.addButton.enabled);
    QUICK_CHECK(strlen(editor->commands.items[19].instruction)==60000);
    for(int language=0;language<2;language++){QUICK_CHECK(kr_mac_set_language(language));[editor languageChanged:nil];QUICK_CHECK(!strcmp(editor->commands.items[0].name,"Сервер & тест 😀"));QUICK_CHECK(editor.commandField.stringValue.length==60000);}
    { NSData *bytes=[NSData dataWithContentsOfFile:ini];QUICK_CHECK(bytes.length>2&&((const unsigned char*)bytes.bytes)[0]==255&&((const unsigned char*)bytes.bytes)[1]==254);
      NSString *text=[[NSString alloc]initWithData:bytes encoding:NSUTF16StringEncoding];QUICK_CHECK([text containsString:@"value=Кириллица 😀"]&&[text containsString:@"SessionTabs=0"]); }
    for(int i=0;i<KR_QUICK_LIMIT;i++){editor.selected=i;[editor remove:nil];}
    [editor reload];QUICK_CHECK(!editor.slots.count);
    { KrQuickCommands again;char *error=NULL;QUICK_CHECK(kr_quick_load(&again,"Update Ubuntu",&error));BOOL empty=!again.items[0].name;kr_quick_free(&again);sfree(error);QUICK_CHECK(empty); }
finish:
    [editor.window close];kr_mac_set_root(original.UTF8String);[fm removeItemAtPath:path error:nil];
    if(ok)puts("PASS: native quick-command editor, 20 slots, Unicode/ampersands, 60000-byte command, Enter, immediate save/delete, UTF16 BOM and other sections, no reseeding");
    return ok?0:1;
#undef QUICK_CHECK
}
