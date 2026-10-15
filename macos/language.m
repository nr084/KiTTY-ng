/* Same portable UILanguage preference as Windows, in values.ini. */
#import <AppKit/AppKit.h>
#include "putty.h"
#include "storage.h"
#include "portable.h"
#include "language.h"
#include "cli-language.h"
static int selected=-1;
static NSString *preferences(NSStringEncoding *encoding){
    *encoding=NSUTF8StringEncoding;
    const char *base=kr_mac_root();if(!base)return @"";NSString *path=[[NSString stringWithUTF8String:base]stringByAppendingPathComponent:@"values.ini"];NSData *data=[NSData dataWithContentsOfFile:path];if(!data){*encoding=NSUTF8StringEncoding;return @"";}
    const unsigned char *p=data.bytes;*encoding=data.length>=2&&p[0]==255&&p[1]==254?NSUTF16LittleEndianStringEncoding:NSUTF8StringEncoding;
    NSString *value=[[NSString alloc]initWithData:data encoding:*encoding];if(!value){*encoding=NSWindowsCP1251StringEncoding;value=[[NSString alloc]initWithData:data encoding:*encoding];}if([value hasPrefix:@"\uFEFF"])value=[value substringFromIndex:1];return value;
}
int kr_mac_language(void){
    if(selected>=0)return selected;selected=[[[NSLocale preferredLanguages]firstObject]hasPrefix:@"ru"]?1:0;
    NSStringEncoding encoding;NSString *value=preferences(&encoding);BOOL active=NO;
    for(NSString *raw in [value componentsSeparatedByCharactersInSet:NSCharacterSet.newlineCharacterSet]){NSString *line=[raw stringByTrimmingCharactersInSet:NSCharacterSet.whitespaceAndNewlineCharacterSet];if([line hasPrefix:@"["])active=[line.lowercaseString isEqualToString:@"[values]"];else if(active){NSRange equals=[line rangeOfString:@"="];if(equals.location!=NSNotFound&&[[[line substringToIndex:equals.location]stringByTrimmingCharactersInSet:NSCharacterSet.whitespaceCharacterSet]isEqualToString:@"UILanguage"]){NSString *choice=[[line substringFromIndex:equals.location+1]stringByTrimmingCharactersInSet:NSCharacterSet.whitespaceCharacterSet];if([choice isEqualToString:@"0"]||[choice isEqualToString:@"1"])selected=choice.intValue;}}}
    const char *override=getenv("KITTY_NG_LANGUAGE");if(override){if(!strcmp(override,"ru"))selected=1;else if(!strcmp(override,"en"))selected=0;}    NSArray *arguments=NSProcessInfo.processInfo.arguments;for(NSUInteger i=1;i+1<arguments.count;i++)if([arguments[i]isEqualToString:@"--language"]){if([arguments[i+1]isEqualToString:@"ru"])selected=1;else if([arguments[i+1]isEqualToString:@"en"])selected=0;}
return selected;
}
bool kr_mac_set_language(int language){
    if(language!=0&&language!=1)return false;const char *base=kr_mac_root();if(!base||strstr(base,"/AppTranslocation/"))return false;
    NSStringEncoding encoding;NSString *old=preferences(&encoding);if(!old)return false;NSMutableString *out=[NSMutableString new];BOOL active=NO,written=NO,section=NO;
    for(NSString *raw in [old componentsSeparatedByString:@"\n"]){NSString *line=[raw stringByTrimmingCharactersInSet:NSCharacterSet.whitespaceAndNewlineCharacterSet];if([line hasPrefix:@"["]){if(active&&!written){[out appendFormat:@"UILanguage=%d\n",language];written=YES;}active=[line.lowercaseString isEqualToString:@"[values]"];section|=active;}
        NSRange equal=[line rangeOfString:@"="];if(active&&equal.location!=NSNotFound&&[[[line substringToIndex:equal.location]stringByTrimmingCharactersInSet:NSCharacterSet.whitespaceCharacterSet]isEqualToString:@"UILanguage"]){if(!written){[out appendFormat:@"UILanguage=%d\n",language];written=YES;}}else{[out appendString:raw];[out appendString:@"\n"];}}
    if(!written){if(!section)[out appendString:@"[values]\n"];[out appendFormat:@"UILanguage=%d\n",language];}
    NSData *data=[out dataUsingEncoding:encoding];if(!data)return false;
    if(encoding==NSUTF16LittleEndianStringEncoding&&!(data.length>=2&&((const unsigned char*)data.bytes)[0]==255&&((const unsigned char*)data.bytes)[1]==254)){NSMutableData *bom=[NSMutableData dataWithBytes:"\xff\xfe" length:2];[bom appendData:data];data=bom;}
    NSString *path=[[NSString stringWithUTF8String:base]stringByAppendingPathComponent:@"values.ini"];if(![data writeToFile:path options:NSDataWritingAtomic error:nil])return false;selected=language;kr_mac_cli_set_language(language?"ru":"en");return true;
}

static const struct{const char *en,*ru;}ui_strings[]={
#include "translations.h"
#include "../windows/kittyng-translations.h"
};
NSString *kr_mac_localize_string(NSString *text){
    /* Language identifiers remain readable and stable in every locale. */
    if([text isEqualToString:@"English"]||[text isEqualToString:@"Русский"])return text;

    for(size_t i=0;i<lenof(ui_strings);i++){NSString *en=[NSString stringWithUTF8String:ui_strings[i].en],*ru=[NSString stringWithUTF8String:ui_strings[i].ru];if([text isEqualToString:en]||[text isEqualToString:ru])return kr_mac_language()?ru:en;}return text;
}
void kr_mac_localize_menu(NSMenu *menu){
    menu.title=kr_mac_localize_string(menu.title);
    for(NSMenuItem *item in menu.itemArray){item.title=kr_mac_localize_string(item.title);if(item.submenu)kr_mac_localize_menu(item.submenu);}
}
void kr_mac_localize_view(NSView *view){
    if([view.identifier isEqualToString:@"KiTTYNG.RawData"])return;
    /* Never recurse into AppKit controls: their private buttons/cells have
     * implementation titles (e.g. search), not application UI strings. */
    if([view isKindOfClass:NSSearchField.class]){((NSSearchField*)view).placeholderString=kr_mac_localize_string(@"Quick search");return;}
    if([view isKindOfClass:NSPopUpButton.class]){for(NSMenuItem *item in ((NSPopUpButton*)view).itemArray)item.title=kr_mac_localize_string(item.title);return;}
    if([view isKindOfClass:NSButton.class]){((NSButton*)view).title=kr_mac_localize_string(((NSButton*)view).title);return;}
    if([view isKindOfClass:NSTextField.class]){NSTextField *field=(NSTextField*)view;if(field.placeholderString)field.placeholderString=kr_mac_localize_string(field.placeholderString);if(!((NSTextField*)view).editable)((NSTextField*)view).stringValue=kr_mac_localize_string(((NSTextField*)view).stringValue);return;}
    if([view isKindOfClass:NSTableView.class]){for(NSTableColumn *column in ((NSTableView*)view).tableColumns)column.title=kr_mac_localize_string(column.title);return;}
    if([view isKindOfClass:NSTabView.class]){for(NSTabViewItem *item in ((NSTabView*)view).tabViewItems){item.label=kr_mac_localize_string(item.label);kr_mac_localize_view(item.view);}return;}
    if([view isKindOfClass:NSControl.class])return;
    for(NSView *child in view.subviews)kr_mac_localize_view(child);
}

void kr_mac_add_edit_menu(NSMenu *menu){
NSMenuItem *editItem=[NSMenuItem new];[menu addItem:editItem];
    NSMenu *editMenu=[[NSMenu alloc]initWithTitle:kr_mac_localize_string(@"Edit")];editItem.submenu=editMenu;
    [editMenu addItemWithTitle:kr_mac_localize_string(@"Cut") action:@selector(cut:) keyEquivalent:@"x"];
    [editMenu addItemWithTitle:kr_mac_localize_string(@"Copy") action:@selector(copy:) keyEquivalent:@"c"];
    [editMenu addItemWithTitle:kr_mac_localize_string(@"Paste") action:@selector(paste:) keyEquivalent:@"v"];
    [editMenu addItemWithTitle:kr_mac_localize_string(@"Select all") action:@selector(selectAll:) keyEquivalent:@"a"];
}
