/* Real LVGL rendering and navigation, with the production mock library. */
#include "../app/ui/ui.c"
#include "src/display/lv_display_private.h"

static uint32_t frame[720][1280];
static uint8_t draw_buffer[1280 * 40 * 4];
static int fast_cues,exit_cues,click_cues;
static lv_indev_t *touch;
static lv_point_t touch_point;
static bool touch_down;
static void check(bool ok, const char *message)
{ if(!ok) { fprintf(stderr,"FAIL: %s\n",message); exit(1); } }
static void flush(lv_display_t *display, const lv_area_t *area, uint8_t *pixels)
{
    for(int y=area->y1;y<=area->y2;y++)
        memcpy(&frame[y][area->x1],pixels+(y-area->y1)*lv_area_get_width(area)*4,
               lv_area_get_width(area)*4);
    lv_display_flush_ready(display);
}
static void pump(int ms)
{ for(int i=0;i<ms;i+=10) { lv_tick_inc(10);lv_timer_handler(); } }
static void feedback(int type) { if(type==UI_FEEDBACK_FAST_SCROLL) fast_cues++;else if(type==UI_FEEDBACK_FAST_SCROLL_EXIT)exit_cues++;else if(type==UI_FEEDBACK_CLICK)click_cues++; }

static void read_touch(lv_indev_t *indev, lv_indev_data_t *data)
{ LV_UNUSED(indev);data->point=touch_point;data->state=touch_down?LV_INDEV_STATE_PRESSED:LV_INDEV_STATE_RELEASED; }
static void pointer(int x,int y,bool down)
{ touch_point=(lv_point_t){x,y};touch_down=down;lv_indev_read(touch);pump(30); }
static lv_obj_t *first_label(lv_obj_t *obj)
{
    if(lv_obj_check_type(obj,&lv_label_class))return obj;
    for(uint32_t i=0;i<lv_obj_get_child_count(obj);i++) {
        lv_obj_t *found=first_label(lv_obj_get_child(obj,i));if(found)return found;
    }
    return NULL;
}
static void press_control(lv_obj_t *obj,bool styled)
{
    check(obj && lv_obj_is_visible(obj),"Press fixture must have a visible target");
    lv_area_t bounds,before,after;lv_obj_get_coords(obj,&bounds);
    lv_obj_t *label=first_label(obj);
    check(label!=NULL,"Press fixture must include a label");
    lv_state_t state=lv_obj_get_state(label);
    lv_color_t color=lv_obj_get_style_text_color(label,0);
    lv_obj_get_coords(label,&before);
    pointer((bounds.x1+bounds.x2)/2,(bounds.y1+bounds.y2)/2,true);
    if(lv_obj_has_state(obj,LV_STATE_USER_2)!=styled)
        fprintf(stderr,"Press mismatch: screen=%d label=%s bounds=(%ld,%ld)-(%ld,%ld) expected=%d state=%u\n",
            g_current_screen,lv_label_get_text(label),(long)bounds.x1,(long)bounds.y1,(long)bounds.x2,(long)bounds.y2,styled,lv_obj_get_state(obj));
    check(lv_obj_has_state(obj,LV_STATE_USER_2)==styled,
          "Touch targets must show the press state, except Now Playing content");
    if(styled) {
        lv_obj_get_coords(label,&after);
        check(lv_area_is_equal(&before,&after),"Press feedback must not shift text or change row geometry");
        check(lv_color_eq(lv_obj_get_style_text_color(label,0),lv_color_hex(g_theme->on_selection)),
              "Pressed text must stay legible, including nested and already-selected labels");
        if((frame[bounds.y2][(bounds.x1+bounds.x2)/2]&0xffffff)!=g_theme->on_selection)
            fprintf(stderr,"Press pixels: screen=%d label=%s expected=%06lx actual=%06lx\n",g_current_screen,
                lv_label_get_text(label),(unsigned long)g_theme->on_selection,
                (unsigned long)(frame[bounds.y2][(bounds.x1+bounds.x2)/2]&0xffffff));
        check((frame[bounds.y2][(bounds.x1+bounds.x2)/2]&0xffffff)==g_theme->on_selection,
              "A real pointer press must render the square inset outline");
    }
    /* Cancel through the input device so destructive/playback actions do not
     * run; LVGL delivers PRESS_LOST, as it does when a drag takes over. */
    lv_indev_wait_release(touch);
    pointer(touch_point.x,touch_point.y,false);
    check(!lv_obj_has_state(obj,LV_STATE_USER_2),"Cancelling a touch must clear its temporary highlight");
    check(lv_obj_get_state(label)==state && lv_color_eq(lv_obj_get_style_text_color(label,0),color),
          "Cancelling a press must restore the original selection and label color");
}
static void pressed_controls(void)
{
    touch=lv_indev_create();lv_indev_set_type(touch,LV_INDEV_TYPE_POINTER);
    lv_indev_set_display(touch,lv_display_get_default());lv_indev_set_read_cb(touch,read_touch);
    ui_set_touch_enabled(true);ui_attach_touch(touch);
    ui_select_screen(SCR_ALBUMS);pump(600);albums_select(0);pump(100);
    press_control(lv_obj_get_parent(g_status_time),true);
    press_control(g_menu_items[SCR_ALBUMS],true);
    press_control(albums_rows[0],true);
    press_control(lv_obj_get_child(albums_right_content,1),false); /* The scroll viewport itself is not a button. */
    press_control(lv_obj_get_child(lv_obj_get_child(albums_right_content,1),0),true);
    press_control(g_mini,false);
    ui_select_screen(SCR_ARTISTS);pump(600);artists_select(0);pump(100);
    press_control(artists_rows[0],true);
    press_control(lv_obj_get_child(lv_obj_get_child(artists_right_content,1),0),true);
    ui_select_screen(SCR_SONGS);pump(600);songs_select(0);pump(100);
    press_control(songs_rows[0],true);
    ui_select_screen(SCR_PLAYLISTS);pump(600);
    press_control(lv_obj_get_child(playlists_left_list,0),true);
    ui_select_screen(SCR_SETTINGS);pump(600);
    press_control(lv_obj_get_child(settings_left_list,0),true);
    settings_open((void *)(intptr_t)SETTING_SCREEN);pump(200);
    press_control(g_focus[g_focus_index].obj,true);
    ui_select_screen(SCR_BLUETOOTH);pump(600);
    press_control(g_focus[0].obj,true);
    ui_select_screen(SCR_NOW_PLAYING);pump(600);
    press_control(lv_obj_get_parent(np_play_icon),false);
    press_control(np_star_btn,false);
    press_control(np_shuffle_btn,false);
    np_toggle_queue();pump(600);
    press_control(g_focus[np_queue_focus_start].obj,false);
    np_toggle_queue();pump(200);
    np_gradient_visibility(true);pump(100);
    press_control(lv_obj_get_parent(g_status_time),true);
    press_control(g_menu_items[SCR_NOW_PLAYING],true);
    np_gradient_visibility(false);pump(100);
    ui_attach_touch(NULL);lv_indev_delete(touch);touch=NULL;
}

static void alpha_fade(void)
{
    lv_obj_t *panel=lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(panel);lv_obj_set_pos(panel,300,200);lv_obj_set_size(panel,200,60);
    lv_obj_set_style_bg_color(panel,lv_color_hex(0x205090),0);
    lv_obj_set_style_bg_grad_color(panel,lv_color_hex(0x30a060),0);
    lv_obj_set_style_bg_grad_dir(panel,LV_GRAD_DIR_HOR,0);
    lv_obj_set_style_bg_opa(panel,LV_OPA_COVER,0);
    const char *texts[]={"MMMMMMMMMMMMMMMMMMMM",
        "An album with wide W and narrow iii letters and several words",
        "Été — déjà vu • café / Straße — 日本語 and more metadata"};
    for(unsigned sample=0;sample<sizeof(texts)/sizeof(texts[0]);sample++) {
        lv_obj_t *label=make_label(panel,texts[sample],FONT_H1,0xffffff);
        lv_obj_set_pos(label,10,5);lv_obj_set_width(label,120);ui_text_fade(label);
        if(sample==1) ((lv_label_t*)label)->offset.x=-37;
        if(sample==2) lv_obj_set_style_text_letter_space(label,1,0);
        lv_obj_remove_flag(label,LV_OBJ_FLAG_SEND_DRAW_TASK_EVENTS);pump(100);
        lv_area_t bounds;lv_obj_get_content_coords(label,&bounds);
        uint32_t normal[60][16],background[60][16];
        int h=LV_MIN(60,lv_area_get_height(&bounds));
        for(int y=0;y<h;y++)for(int x=0;x<16;x++)normal[y][x]=frame[bounds.y1+y][bounds.x2-15+x];
        lv_obj_set_hidden(label,true);pump(100);
        for(int y=0;y<h;y++)for(int x=0;x<16;x++)background[y][x]=frame[bounds.y1+y][bounds.x2-15+x];
        lv_obj_set_hidden(label,false);lv_obj_add_flag(label,LV_OBJ_FLAG_SEND_DRAW_TASK_EVENTS);pump(100);
        int ink=0;
        for(int y=0;y<h;y++)for(int x=0;x<16;x++) {
            uint32_t actual=frame[bounds.y1+y][bounds.x2-15+x];
            if(normal[y][x]!=background[y][x]) ink++;
            for(int shift=0;shift<24;shift+=8) {
                int bg=(background[y][x]>>shift)&255,fg=(normal[y][x]>>shift)&255;
                int expected=bg+(fg-bg)*(15-x)/15;
            check(abs((int)((actual>>shift)&255)-expected)<=4,
                      "Fading glyphs must blend into the live colored background, without a solid mask");
            }
        }
        check(ink>20,"Fade oracle must include real glyph pixels");lv_obj_delete(label);pump(100);
    }
    lv_obj_delete(panel);pump(100);
}

static void now_playing_keys(void)
{
    player_album_t album;
    player_track_t first,second;
    check(player_album_get(0,&album) && player_album_track_get(album.id,0,&first) &&
          player_album_track_get(album.id,1,&second),"Playback key fixture needs two album tracks");
    player_set_shuffle(false);
    player_play_album(album.id);
    player_queue_jump(1);
    player_seek_sec(10);
    ui_select_screen(SCR_NOW_PLAYING);pump(500);
    if(np_queue_visible) {np_toggle_queue();pump(400);}
    player_snapshot_t snap;
    ui_handle_key(UI_KEY_UP);player_get_snapshot(&snap);
    check(snap.track.id==second.id && snap.elapsed_sec==0,
          "Now Playing Up must restart a track after the restart threshold");
    ui_handle_key(UI_KEY_UP);pump(400);player_get_snapshot(&snap);
    check(snap.track.id==first.id,"Now Playing Up near the start must select the previous track");
    ui_handle_key(UI_KEY_DOWN);pump(400);player_get_snapshot(&snap);
    check(snap.track.id==second.id,"Now Playing Down must skip to the next track");
    ui_handle_key(UI_KEY_ENTER);player_get_snapshot(&snap);
    check(snap.status==PLAYER_PAUSED && snap.track.id==second.id,"Now Playing Enter must pause the current track");
    ui_handle_key(UI_KEY_ENTER);player_get_snapshot(&snap);
    check(snap.status==PLAYER_PLAYING,"Now Playing Enter must resume playback");

    bool starred=snap.starred;
    ui_handle_key(UI_KEY_CONTEXT);
    ui_handle_key(UI_KEY_DOWN);ui_handle_key(UI_KEY_DOWN);
    check(g_ctx_menu_open && g_ctx_menu_focus==g_ctx_star_item,"Context arrows must retain menu navigation");
    ui_handle_key(UI_KEY_ENTER);player_get_snapshot(&snap);
    check(snap.starred!=starred && snap.status==PLAYER_PLAYING && snap.track.id==second.id,
          "Context Enter must activate Star rather than pause playback");
    ui_handle_key(UI_KEY_ENTER);ui_handle_key(UI_KEY_BACK);

    ui_handle_key(UI_KEY_PAGE_UP);ui_handle_key(UI_KEY_PAGE_DOWN);pump(300);
    player_get_snapshot(&snap);
    check(!np_queue_visible && g_current_screen==SCR_NOW_PLAYING && snap.track.id==second.id,
          "Page Up/Down with no list must not open Up Next, switch tabs or change playback");
    ui_handle_key(UI_KEY_QUEUE);pump(600);
    check(np_queue_visible,"The explicit Queue action must open Up Next");
    focus_select(np_queue_focus_start);
    ui_handle_key(UI_KEY_PAGE_UP);pump(100);
    check(np_queue_visible && g_focus_index==np_queue_focus_start,
          "Page Up at the queue start must keep Up Next open and retain queue focus");
    int page_rows=lv_obj_get_content_height(np_queue_list)/NP_QUEUE_ROW_H;
    int page_target=np_queue_base+(page_rows>1?page_rows-1:1);
    if(page_target>=np_queue_total)page_target=np_queue_total-1;
    ui_handle_key(UI_KEY_PAGE_DOWN);pump(100);player_get_snapshot(&snap);
    check(np_queue_visible && np_queue_selected==page_target && snap.track.id==second.id,
          "Page Down must page open queue rows without changing playback");
    ui_handle_key(UI_KEY_PAGE_UP);pump(100);
    check(np_queue_visible && g_focus_index==np_queue_focus_start,
          "Page Up must return to the queue start without focusing transport controls");
    focus_select(np_queue_focus_start);
    ui_handle_key(UI_KEY_DOWN);player_get_snapshot(&snap);
    check(g_focus_index==np_queue_focus_start+1 && snap.track.id==second.id,
          "Up Next Down must move selection without changing the playing track");
    ui_handle_key(UI_KEY_UP);
    check(g_focus_index==np_queue_focus_start,"Up Next Up must move selection to the previous row");
    player_track_t queued;
    check(player_queue_at((int)(intptr_t)g_focus[g_focus_index].user,&queued),"Selected Up Next row must resolve a queue track");
    ui_handle_key(UI_KEY_ENTER);pump(400);player_get_snapshot(&snap);
    check(snap.track.id==queued.id && snap.status==PLAYER_PLAYING,"Up Next Enter must play the selected queue row");
    ui_handle_key(UI_KEY_BACK);pump(400);
    check(!np_queue_visible,"Back still closes Up Next");
    ui_handle_key(UI_KEY_QUEUE);pump(400);
    ui_handle_key(UI_KEY_QUEUE);pump(400);
    check(!np_queue_visible,"The explicit Queue action must also close Up Next");
    ui_handle_key(UI_KEY_SCREEN_PREV);pump(400);
    check(np_queue_visible,"Previous-tab navigation at Now Playing must retain its queue shortcut");
    ui_handle_key(UI_KEY_BACK);pump(300);
    ui_handle_key(UI_KEY_SCREEN_NEXT);pump(500);
    check(g_current_screen==SCR_ALBUMS,"Next-tab navigation must still open Albums");
    int selected=albums_selected;
    ui_handle_key(UI_KEY_PAGE_DOWN);pump(200);
    check(g_current_screen==SCR_ALBUMS && albums_selected>selected,
          "Page Down in Albums must page its list while retaining the tab");
    ui_handle_key(UI_KEY_PAGE_UP);pump(200);
    check(g_current_screen==SCR_ALBUMS && albums_selected<=selected,
          "Page Up in Albums must page back without switching tabs");
    ui_handle_key(UI_KEY_SCREEN_PREV);
    ui_handle_key(UI_KEY_PAGE_UP);ui_handle_key(UI_KEY_PAGE_DOWN);pump(500);
    check(g_current_screen==SCR_NOW_PLAYING && !np_queue_visible,
          "Page keys immediately after switching to Now Playing must leave Up Next closed");
}

static int menu_item_named(const char *name)
{
    for(int i=0;i<g_ctx_menu_item_count;i++)
        if(!strcmp(lv_label_get_text(g_ctx_menu_items[i].label),name)) return i;
    return -1;
}
static void menu_choose(const char *name)
{
    int index=menu_item_named(name);
    check(index>=0,"Requested context action must exist");
    context_menu_focus_set(index);ui_handle_key(UI_KEY_ENTER);pump(300);
}
static void menu_open_key(context_kind_t kind,uint64_t id)
{
    ui_handle_key(UI_KEY_CONTEXT);pump(30);
    check(g_ctx_menu_open && g_ctx_target.kind==kind && g_ctx_target.id==id,
          "Context must capture the selected entity, not the playing song");
    lv_area_t bounds;lv_obj_get_coords(lv_obj_get_parent(g_ctx_menu_items[0].row),&bounds);
    check(bounds.y1>=0 && bounds.y2<SCREEN_H,"Context menu must fit the screen in every theme");
}
static void hold_row_for_menu(lv_obj_t *row,context_kind_t kind,uint64_t id)
{
    check(row && lv_obj_is_visible(row),"Long-press fixture needs a visible row");
    lv_area_t bounds;lv_obj_get_coords(row,&bounds);
    pointer((bounds.x1+bounds.x2)/2,(bounds.y1+bounds.y2)/2,true);
    pump(650);
    check(g_ctx_menu_open && g_ctx_target.kind==kind && g_ctx_target.id==id,
          "Holding a row must open context for that exact row");
    pointer(touch_point.x,touch_point.y,false);pump(100);
    check(g_ctx_menu_open,"Releasing a context hold must not run its ordinary click");
}
static void library_context_menus(void)
{
    player_album_t album;player_track_t track,other;player_artist_t artist;
    check(player_album_get(0,&album),"Context fixture needs an album");
    player_play_album(album.id);player_queue_jump(0);player_seek_sec(10);
    player_snapshot_t before,after;player_get_snapshot(&before);
    ui_select_screen(SCR_ALBUMS);pump(600);albums_select(0);pump(100);
    int scroll=lv_obj_get_scroll_y(albums_left_list),selected=albums_selected;
    menu_open_key(CTX_ALBUM,album.id);
    int add_icon=menu_item_named("Add to Queue"),next_icon=menu_item_named("Play Next");
    check(add_icon>=0 && next_icon>=0 &&
          lv_obj_check_type(g_ctx_menu_items[add_icon].icon,&lv_image_class) &&
          lv_image_get_src(g_ctx_menu_items[add_icon].icon)==&context_add_to_queue &&
          lv_image_get_src(g_ctx_menu_items[next_icon].icon)==&context_play_next,
          "Queue actions must use the requested NRK icon assets");
    context_menu_focus_set(add_icon);
    check(lv_color_eq(lv_obj_get_style_image_recolor(g_ctx_menu_items[add_icon].icon,0),lv_color_hex(COL_TEXT)) &&
          lv_color_eq(lv_obj_get_style_image_recolor(g_ctx_menu_items[next_icon].icon,0),lv_color_hex(COL_MUTED)),
          "Image icons must use the same focused and unfocused colors as menu glyphs");
    context_menu_focus_set(0);
    check(menu_item_named("Play")>=0 && menu_item_named("Shuffle Play")>=0 &&
          menu_item_named("Add to Queue")>=0 && menu_item_named("Play Next")>=0,
          "Album menus must expose playback and queue actions");
    ui_handle_key(UI_KEY_BACK);
    check(albums_selected==selected && lv_obj_get_scroll_y(albums_left_list)==scroll,
          "Dismissing context must preserve album selection and viewport");
    int count=player_queue_count();
    menu_open_key(CTX_ALBUM,album.id);menu_choose("Add to Queue");
    check(player_queue_count()==count+player_album_track_count(album.id),"Add album must append its complete track list");
    for(int i=0;i<player_album_track_count(album.id);i++) {
        player_album_track_get(album.id,i,&track);player_queue_at(count+i,&other);
        check(track.id==other.id,"Appended album must preserve its display order");
    }
    player_get_snapshot(&after);
    check(after.track.id==before.track.id && after.elapsed_sec>=before.elapsed_sec,
          "Appending an album must preserve playback position");
    albums_open((void *)(intptr_t)0);pump(300);
    int track_focus=-1;
    for(int i=0;i<g_focus_count;i++) if(g_focus[i].activate==albums_track_action && (intptr_t)g_focus[i].user==1) {track_focus=i;break;}
    check(track_focus>=0 && player_album_track_get(album.id,1,&track),"Album detail needs a second song");
    focus_select(track_focus);menu_open_key(CTX_TRACK,track.id);
    bool starred=player_track_starred(track.id),playing_star=player_track_starred(before.track.id);
    menu_choose(starred ? "Unstar" : "Star");
    check(player_track_starred(track.id)!=starred && player_track_starred(before.track.id)==playing_star,
          "Starring a detail song must not change the currently playing song's star");
    ui_handle_key(UI_KEY_BACK);
    menu_open_key(CTX_TRACK,track.id);menu_choose("Play Next");
    player_queue_at(player_queue_index()+1,&other);
    check(other.id==track.id,"Song Play Next must insert after the current song");

    ui_select_screen(SCR_ARTISTS);pump(600);artists_select(0);pump(150);
    check(player_artist_get(0,&artist),"Context fixture needs an artist");
    menu_open_key(CTX_ARTIST,artist.id);count=player_queue_count();menu_choose("Add to Queue");
    check(player_queue_count()==count+player_artist_song_count(artist.id),"Add artist must append all its songs");
    for(int i=0;i<player_artist_song_count(artist.id);i++) {
        player_artist_song_get(artist.id,i,&track);player_queue_at(count+i,&other);
        check(track.id==other.id,"Appended artist must preserve displayed song order");
    }
    artists_open((void *)(intptr_t)0);pump(300);
    track_focus=-1;
    for(int i=0;i<g_focus_count;i++) if(g_focus[i].activate==artists_track_action) {track_focus=i;break;}
    check(track_focus>=0,"Artist detail needs a song focus target");
    focus_select(track_focus);player_artist_song_get(artist.id,(int)(intptr_t)g_focus[track_focus].user,&track);
    menu_open_key(CTX_TRACK,track.id);ui_handle_key(UI_KEY_BACK);

    ui_select_screen(SCR_SONGS);pump(600);songs_select(2);pump(100);
    player_song_get(2,&track);menu_open_key(CTX_TRACK,track.id);
    uint64_t captured=track.id;
    player_next();pump(100);
    check(g_ctx_menu_open && g_ctx_target.id==captured,"A track change must not redirect an open song menu");
    menu_choose("Add to Queue");player_queue_at(player_queue_count()-1,&other);
    check(other.id==captured,"An action after a track change must still target the captured selection");
    menu_open_key(CTX_TRACK,captured);menu_choose("Go to album");
    player_album_get(albums_selected,&album);
    check(g_current_screen==SCR_ALBUMS && albums_detail_open && album.id==track.album_id,
          "Song Go to album must navigate using the selected song");

    ui_select_screen(SCR_NOW_PLAYING);pump(500);
    if(!np_queue_visible) {ui_handle_key(UI_KEY_QUEUE);pump(600);}
    check(g_focus_count>np_queue_focus_start+2,"Queue context fixture needs several upcoming entries");
    focus_select(np_queue_focus_start+2);
    int index=(int)(intptr_t)g_focus[g_focus_index].user;
    player_queue_at(index,&track);uint64_t entry=player_queue_entry(index);
    menu_open_key(CTX_QUEUE,track.id);
    check(g_ctx_target.entry==entry,"Queue menu must capture an occurrence ID");
    count=player_queue_count();menu_choose("Play Next");pump(600);
    check(player_queue_count()==count && player_queue_entry(player_queue_index()+1)==entry,
          "Queue Play Next must move the occurrence without adding a duplicate");
    focus_select(np_queue_focus_start);
    player_queue_at((int)(intptr_t)g_focus[g_focus_index].user,&track);
    menu_open_key(CTX_QUEUE,track.id);menu_choose("Remove from Queue");pump(600);
    check(player_queue_count()==count-1,"Queue Remove must remove one occurrence");
    for(int i=0;i<player_queue_count();i++) check(player_queue_entry(i)!=entry,"Remove must address the selected occurrence");

    touch=lv_indev_create();lv_indev_set_type(touch,LV_INDEV_TYPE_POINTER);
    lv_indev_set_display(touch,lv_display_get_default());lv_indev_set_read_cb(touch,read_touch);ui_set_touch_enabled(true);ui_attach_touch(touch);
    ui_select_screen(SCR_ALBUMS);pump(600);albums_select(0);pump(100);player_album_get(0,&album);
    lv_obj_t *row=NULL;
    for(int i=0;i<ALBUMS_POOL;i++) if(albums_row_indices[i]==0)row=albums_rows[i];
    hold_row_for_menu(row,CTX_ALBUM,album.id);ui_handle_key(UI_KEY_BACK);
    check(!albums_detail_open,"Album context hold must not open detail on release");
    albums_open((void *)(intptr_t)0);pump(300);
    for(int i=0;i<g_focus_count;i++) if(g_focus[i].activate==albums_track_action && (intptr_t)g_focus[i].user==1) {row=g_focus[i].obj;break;}
    player_album_track_get(album.id,1,&track);
    player_get_snapshot(&before);hold_row_for_menu(row,CTX_TRACK,track.id);ui_handle_key(UI_KEY_BACK);
    player_get_snapshot(&after);check(before.track.id==after.track.id,"Song context hold must not start playback");
    ui_select_screen(SCR_SONGS);pump(600);songs_select(0);pump(100);player_song_get(1,&track);
    row=NULL;for(int i=0;i<SONGS_POOL;i++)if(songs_row_indices[i]==1)row=songs_rows[i];
    hold_row_for_menu(row,CTX_TRACK,track.id);ui_handle_key(UI_KEY_BACK);
    ui_select_screen(SCR_ARTISTS);pump(600);artists_select(0);pump(100);player_artist_get(0,&artist);
    row=NULL;for(int i=0;i<ARTISTS_POOL;i++)if(artists_row_indices[i]==0)row=artists_rows[i];
    hold_row_for_menu(row,CTX_ARTIST,artist.id);ui_handle_key(UI_KEY_BACK);
    artists_open((void *)(intptr_t)0);pump(300);
    for(int i=0;i<g_focus_count;i++) if(g_focus[i].activate==artists_track_action) {
        row=g_focus[i].obj;player_artist_song_get(artist.id,(int)(intptr_t)g_focus[i].user,&track);break;
    }
    hold_row_for_menu(row,CTX_TRACK,track.id);ui_handle_key(UI_KEY_BACK);
    ui_select_screen(SCR_NOW_PLAYING);pump(500);
    if(!np_queue_visible) {ui_handle_key(UI_KEY_QUEUE);pump(600);}
    check(g_focus_count>np_queue_focus_start,"Queue long press needs an upcoming entry");
    row=g_focus[np_queue_focus_start].obj;
    player_queue_at((int)(intptr_t)g_focus[np_queue_focus_start].user,&track);
    hold_row_for_menu(row,CTX_QUEUE,track.id);ui_handle_key(UI_KEY_BACK);
    ui_handle_key(UI_KEY_QUEUE);pump(300);
    ui_attach_touch(NULL);lv_indev_delete(touch);touch=NULL;
    player_play_album(album.id);
}

static void context_album_delayed_refresh(void)
{
    ui_select_screen(SCR_NOW_PLAYING);pump(400);
    ui_handle_key(UI_KEY_CONTEXT);ui_handle_key(UI_KEY_ENTER);pump(500);
    check(g_current_screen==SCR_ALBUMS && albums_detail_open &&
          g_focus_index==albums_play_focus,"Go to album must open the current album with Play focused");
    lv_obj_t *left=albums_left_list;
    int count=g_focus_count;
    for(int pass=0;pass<3;pass++) {
        if(pass) {
            for(int i=0;i<g_focus_count;i++) if(g_focus[i].activate==albums_track_action) {
                focus_select(i);break;
            }
        }
        focus_activate_fn action=g_focus[g_focus_index].activate;
        void *user=g_focus[g_focus_index].user;
        lv_obj_set_flex_grow(albums_detail_title,0);
        lv_obj_set_width(albums_detail_title,32);
        lv_obj_update_layout(albums_detail_title);
        ui_marquee_start(albums_detail_title);
        check(g_marquee_label==albums_detail_title,"Crash fixture must own an animated detail title");
        /* A completed thumbnail used to schedule this timer while detail
         * controls were focused. Exercise that deferred path explicitly. */
        albums_schedule_detail();pump(200);
        check(g_focus_count==count,"Delayed album detail refresh must replace, not append, focus entries");
        check(g_focus_index>=0 && g_focus[g_focus_index].activate==action &&
              g_focus[g_focus_index].user==user,"Delayed refresh must retain the focused Play/track action");
        for(int i=0;i<g_focus_count;i++)
            check(lv_obj_is_valid(g_focus[i].obj),"Every focus target must refer to a live widget after artwork refresh");
        check(albums_left_list==left,"Artwork/detail refresh must retain the left album viewport");
        check(!g_marquee_label || lv_obj_is_valid(g_marquee_label),
              "Detail replacement must retire the previous marquee before deleting its label");
        ui_handle_key(UI_KEY_ENTER);pump(300);
        ui_handle_key(UI_KEY_DOWN);ui_handle_key(UI_KEY_UP);pump(100);
    }
}

static void mini_unchanged_poll(void)
{
    ui_select_screen(SCR_SONGS);pump(600);
    lv_refr_now(lv_display_get_default());
    check(lv_display_get_default()->inv_p==0,"Mini-player fixture must start with a clean frame");
    /* Force a transport revision through the slow path, while all visible
     * fields remain unchanged. No labels or styles should be invalidated. */
    g_mini_rev=UINT32_MAX;
    ui_mini_update();
    check(lv_display_get_default()->inv_p==0,"Unchanged MPD fields must not repaint the mini-player");
}

static void artist_viewport(void)
{
    ui_select_screen(SCR_ARTISTS);pump(600);
    artists_select(18);pump(200);
    /* Touch scrolling can leave the selected artist outside the viewport. */
    lv_obj_scroll_to_y(artists_left_list,360,LV_ANIM_OFF);pump(100);
    int saved=lv_obj_get_scroll_y(artists_left_list),selected=artists_selected;
    check(saved>0,"Artist fixture must be scrolled");
    ui_select_screen(SCR_ALBUMS);pump(600);ui_select_screen(SCR_ARTISTS);pump(600);
    check(artists_selected==selected,"Returning to Artists must preserve selection");
    check(lv_obj_get_scroll_y(artists_left_list)==saved,"Returning to Artists must restore the exact viewport, even after touch scroll");
    artists_open((void *)(intptr_t)selected);pump(200);artists_back();pump(200);
    check(lv_obj_get_scroll_y(artists_left_list)==saved,"Artist detail rebuilds must preserve the viewport");
}

static unsigned audited_scrollbars;
static void audit_scrollbars(lv_obj_t *obj)
{
    /* Only explicit vertical viewports; LVGL also defaults decorative objects
     * and buttons to scrollable, even though they never hold a list. */
    if(lv_obj_is_visible(obj) && lv_obj_is_scrollable(obj) &&
       lv_obj_get_scroll_dir(obj)==LV_DIR_VER &&
       lv_obj_get_scrollbar_mode(obj)!=LV_SCROLLBAR_MODE_OFF) {
        lv_scrollbar_mode_t mode=lv_obj_get_scrollbar_mode(obj);
        bool was_scrolled=lv_obj_has_state(obj,LV_STATE_SCROLLED);
        lv_obj_set_scrollbar_mode(obj,LV_SCROLLBAR_MODE_ON);
        for(int state=0;state<2;state++) {
            if(state)lv_obj_add_state(obj,LV_STATE_SCROLLED);
            else lv_obj_remove_state(obj,LV_STATE_SCROLLED);
            if(lv_obj_get_style_radius(obj,LV_PART_SCROLLBAR)!=0 ||
               lv_obj_get_style_pad_right(obj,LV_PART_SCROLLBAR)!=8)
                fprintf(stderr,"Scrollbar style failure: screen=%d width=%ld height=%ld state=%d\n",
                    g_current_screen,(long)lv_obj_get_width(obj),(long)lv_obj_get_height(obj),state);
            check(lv_obj_get_style_radius(obj,LV_PART_SCROLLBAR)==0,"Every scrollbar must have square corners");
            check(lv_obj_get_style_pad_right(obj,LV_PART_SCROLLBAR)==8,"Every scrollbar must have an eight-pixel right inset");
            if(lv_obj_get_scroll_dir(obj)&LV_DIR_VER) {
                lv_area_t horizontal,vertical,bounds,column;
                lv_obj_get_scrollbar_area(obj,&horizontal,&vertical);
                lv_obj_get_coords(obj,&bounds);
                /* The parent spans the visible column, including its padding.
                 * Measuring only the list's bounds would miss the old extra
                 * 16 px (22 px on Bluetooth) between list and column edges. */
                lv_obj_get_coords(lv_obj_get_parent(obj),&column);
                check(vertical.x2>=vertical.x1,"A forced vertical scrollbar must have a real draw area");
                check(bounds.x2==column.x2,"The scrolling viewport must reach the visible column edge");
                check(column.x2-vertical.x2==8,"Actual scrollbar geometry must be eight pixels inside the visible column");
                if(state) {
                    pump(300); /* Complete the theme's opacity transition. */
                    int y=(vertical.y1+vertical.y2)/2;
                    uint32_t color=lv_color_to_u32(lv_obj_get_style_bg_color(obj,LV_PART_SCROLLBAR))&0xffffff;
                    check((frame[y][vertical.x2]&0xffffff)==color,
                          "The scrollbar must render at the column inset without being clipped by old list bounds");
                }
            }
        }
        if(!was_scrolled)lv_obj_remove_state(obj,LV_STATE_SCROLLED);
        lv_obj_set_scrollbar_mode(obj,mode);audited_scrollbars++;
    }
    for(uint32_t i=0;i<lv_obj_get_child_count(obj);i++)audit_scrollbars(lv_obj_get_child(obj,i));
}
static void scrollbar_views(void)
{
    for(int side=0;side<2;side++) {
        ui_set_menubar_left(side==0);pump(100);
        for(int screen=SCR_ALBUMS;screen<SCR_COUNT;screen++) {
            ui_select_screen(screen);pump(600);audit_scrollbars(lv_screen_active());
            lv_obj_t *master=screen==SCR_ALBUMS?albums_left_list:
                screen==SCR_ARTISTS?artists_left_list:
                screen==SCR_PLAYLISTS?playlists_left_list:
                screen==SCR_SETTINGS?settings_left_list:NULL;
            if(master)check(lv_obj_get_content_width(master)==SIDE_PANEL_W-32,
                           "Moving the scrollbar must preserve master-list content and row widths");
            if(screen==SCR_SONGS)check(lv_obj_get_content_width(songs_list)==CONTENT_W-32-14,
                                      "Moving the Songs scrollbar must preserve its text and row widths");
            if(screen==SCR_ALBUMS)albums_open((void *)(intptr_t)0);
            if(screen==SCR_ARTISTS)artists_open((void *)(intptr_t)0);
            if(screen==SCR_PLAYLISTS)playlists_open((void *)(intptr_t)0);
            if(screen==SCR_SETTINGS)settings_open((void *)(intptr_t)SETTING_FAST_SCROLL);
            pump(200);audit_scrollbars(lv_screen_active());
        }
        ui_select_screen(SCR_NOW_PLAYING);pump(300);np_toggle_queue();pump(600);
        audit_scrollbars(lv_screen_active());np_toggle_queue();pump(300);
    }
    ui_set_menubar_left(true);pump(100);
    check(audited_scrollbars>=30,"Audit must cover virtual lists, detail panes, Settings and Up Next with either menu position");
}

static lv_obj_t *cursor_row(void)
{
    if(g_current_screen==SCR_ALBUMS && !albums_detail_open) {
        if(albums_selected<0)return albums_shuffle_row;
        for(int i=0;i<ALBUMS_POOL;i++)if(albums_row_indices[i]==albums_selected)return albums_rows[i];
    }
    else if(g_current_screen==SCR_ARTISTS && !artists_detail_open) {
        for(int i=0;i<ARTISTS_POOL;i++)if(artists_row_indices[i]==artists_selected)return artists_rows[i];
    }
    else if(g_current_screen==SCR_SONGS) {
        for(int i=0;i<SONGS_POOL;i++)if(songs_row_indices[i]==songs_selected)return songs_rows[i];
    }
    else if(g_focus_index>=0 && g_focus_index<g_focus_count)return g_focus[g_focus_index].obj;
    return NULL;
}

static void check_cursor_band(bool fast)
{
    lv_obj_t *row=cursor_row();check(row!=NULL,"Cursor fixture must have a bound row");
    uint32_t expected=fast?g_theme->fast_scroll_band:COL_ACCENT;
    check(lv_color_eq(lv_obj_get_style_border_color(row,0),lv_color_hex(expected)),
          "The selected row's left band must reflect the scroll mode");
    check(lv_obj_get_style_border_side(row,0)==LV_BORDER_SIDE_LEFT &&
          lv_obj_get_style_border_width(row,0)==3,"Scroll mode must preserve the three-pixel left band");
    lv_obj_update_layout(lv_screen_active());lv_refr_now(lv_display_get_default());
    lv_area_t bounds,viewport;lv_obj_get_coords(row,&bounds);
    lv_obj_get_content_coords(lv_obj_get_parent(row),&viewport);
    int top=LV_MAX(bounds.y1,viewport.y1),bottom=LV_MIN(bounds.y2,viewport.y2);
    check(top<=bottom && top>=0 && bottom<720,"Cursor band must be in the visible viewport");
    check((frame[(top+bottom)/2][bounds.x1+1]&0xffffff)==expected,
          "The real rendered cursor band must show the mode color");
}

static void fast_navigation(void)
{
    g_system_config.fast_scroll_hold_ms=150; /* Exercise the shortest selectable hold. */
    g_system_config.fast_scroll_enabled=true;
    g_system_config.fast_scroll_pages=2;g_system_config.sound_mode=2;g_system_config.sound_enabled=true;
    ui_set_feedback_cb(feedback);ui_select_screen(SCR_ALBUMS);pump(600);
    albums_select(0);pump(200);
    lv_obj_t *context;int rows=ui_fast_scroll_rows(&context);
    check(rows==8,"Albums must use eight visible rows per fast notch");
    uint32_t now=lv_tick_get();
    uint32_t overlays=lv_obj_get_child_count(lv_layer_top());
    ui_handle_key_repeat_timed(UI_KEY_DOWN,rows*2,now-500,now);
    check(albums_selected==rows*3-1,"Threshold crossing must apply a page jump once and coalesce rendering");
    check(fast_cues==1 && g_fast_scroll.active,"Entering fast scroll must produce exactly one special cue");
    check(lv_obj_get_child_count(lv_layer_top())==overlays,"Fast scrolling must not create an overlay indicator");
    pump(40);check_cursor_band(true);
    /* A queued BLE report can map one tick behind its predecessor because
     * the LVGL tick and report age are sampled at different resolutions. */
    now=lv_tick_get();int previous=albums_selected;
    ui_handle_key_repeat_timed(UI_KEY_DOWN,1,now,now);
    ui_handle_key_repeat_timed(UI_KEY_DOWN,1,now-1,now-1);
    check(g_fast_scroll.active && fast_cues==1 && albums_selected==previous+rows*2,
          "A slightly older mapped timestamp must keep fast mode and page steps");
    int selected_slot=-1;
    for(int i=0;i<ALBUMS_POOL;i++) if(albums_row_indices[i]==albums_selected) selected_slot=i;
    check(selected_slot>=0,"Fixture must have a selected virtual row");
    int bound=albums_row_indices[selected_slot];albums_row_indices[selected_slot]=-1;
    ui_fast_scroll_visual_update(NULL);
    check(g_fast_scroll.active && fast_cues==1,
          "A temporarily unbound cursor row must not cancel fast scrolling");
    check(lv_color_eq(lv_obj_get_style_border_color(albums_rows[selected_slot],0),lv_color_hex(g_theme->fast_scroll_band)),
          "Temporary unbinding must not reset the row's fast band color");
    albums_row_indices[selected_slot]=bound;ui_fast_scroll_visual_update(NULL);pump(40);
    check(g_fast_scroll.active,"Binding a virtual row must retain fast mode");check_cursor_band(true);
    int saved_scroll=lv_obj_get_scroll_y(albums_left_list);
    lv_obj_scroll_to_y(albums_left_list,0,LV_ANIM_OFF);ui_fast_scroll_visual_update(NULL);
    check(g_fast_scroll.active,"An offscreen cursor must not cancel fast mode");
    lv_obj_scroll_to_y(albums_left_list,saved_scroll,LV_ANIM_OFF);ui_fast_scroll_visual_update(NULL);pump(40);
    check(g_fast_scroll.active,"Restoring the viewport must retain fast mode");check_cursor_band(true);
    for(int step=0;step<20;step++) {
        pump(50);ui_handle_key(UI_KEY_DOWN);
        if(!g_fast_scroll.active || fast_cues!=1)
            fprintf(stderr,"Sustained spin reset: step=%d selected=%d cues=%d active=%d\n",step,albums_selected,fast_cues,g_fast_scroll.active);
        check(g_fast_scroll.active && fast_cues==1,
              "A continuous page spin must survive recycled rows and reaching the list end");
        check_cursor_band(true);
    }
    albums_select(rows*3-1);
    int old=albums_selected;ui_handle_key(UI_KEY_DOWN);
    check(albums_selected==old+rows && fast_cues==1,"One fast notch must move exactly one page without another entry cue");
    int exits_before=exit_cues;
    ui_handle_key(UI_KEY_UP);
    check(exit_cues==exits_before+1,"A direction reversal must play one exit cue");
    ui_fast_scroll_visual_update(NULL);
    check(exit_cues==exits_before+1,"Inactive indicator refresh must not repeat the exit cue");
    check(albums_selected==old+rows-1 && !g_fast_scroll.active,"A reversal must immediately return to one-row movement");
    check_cursor_band(false);
    for(int i=0;i<14;i++) { pump(30);ui_handle_key(UI_KEY_UP); }
    check(!g_fast_scroll.active,"Reverse spin must first rebuild its own physical velocity");
    pump(30);ui_handle_key(UI_KEY_UP);
    check(g_fast_scroll.active && fast_cues==2,"A sustained reverse spin must re-enter fast mode without losing its first notch");
    ui_handle_key(UI_KEY_UP);ui_handle_key(UI_KEY_UP);pump(40);
    check(albums_selected==-1 && g_fast_scroll.active && fast_cues==2,
          "Reaching Shuffle All must preserve the gesture and not add entry cues");
    ui_fast_scroll_reset();now=lv_tick_get();
    ui_handle_key_repeat_timed(UI_KEY_DOWN,rows*2,now-500,now);pump(100);
    check(g_fast_scroll.active,"A 100 ms pause must preserve fast mode");check_cursor_band(true);
    old=albums_selected;int cues=fast_cues;ui_handle_key(UI_KEY_DOWN);
    check(albums_selected==old+rows && fast_cues==cues,
          "Resuming after 100 ms must keep page steps without repeating the entry cue");
    exits_before=exit_cues;
    pump(200);
    check(exit_cues==exits_before+1,"Idle expiry must play one descending exit cue");
    pump(200);check(exit_cues==exits_before+1,"Idle exit cue must not repeat");
    check(!g_fast_scroll.active,"Pausing must clear fast mode");check_cursor_band(false);
    old=albums_selected;ui_handle_key(UI_KEY_DOWN);check(albums_selected==old+1,"First notch after a pause must be precise");
    ui_fast_scroll_reset();g_system_config.fast_scroll_enabled=false;albums_select(0);
    ui_handle_key_repeat(UI_KEY_DOWN,20);
    check(albums_selected==20 && !g_fast_scroll.active && g_system_config.fast_scroll_pages==2,
          "Disabled must retain one row per notch without losing the activation threshold");
    g_system_config.fast_scroll_enabled=true;ui_fast_scroll_reset();
    ui_select_screen(SCR_SETTINGS);pump(600);settings_open((void *)(intptr_t)SETTING_FAST_SCROLL);pump(200);
    check(g_focus_count==13 && g_focus_index==0,"Fast Scroll must focus its top toggle, three trigger times, five thresholds and four hold times");
    check(!strcmp(lv_label_get_text(settings_fast_trigger_checks[0]),ICON_CHECK),
          "550 ms must be selected by default");
    check(lv_obj_get_index(lv_obj_get_parent(settings_fast_trigger_checks[0])) <
          lv_obj_get_index(lv_obj_get_parent(settings_fast_scroll_checks[0])),
          "Trigger Time choices must appear before the page thresholds");
    for(uint32_t i=0;i<lv_obj_get_child_count(settings_right_content);i++) {
        lv_obj_t *text=first_label(lv_obj_get_child(settings_right_content,i));
        check(!text || strcmp(lv_label_get_text(text),"Off"),"Threshold choices must not contain Off");
    }
    ui_handle_key(UI_KEY_ENTER);
    check(!g_system_config.fast_scroll_enabled && g_system_config.fast_scroll_pages==2 &&
          !strcmp(lv_label_get_text(settings_fast_scroll_enabled_value),"Disabled"),
          "The top control must disable Fast Scroll and preserve its threshold");
    for(int i=0;i<3;i++) {
        ui_handle_key(UI_KEY_DOWN);ui_handle_key(UI_KEY_ENTER);
        check(g_system_config.fast_scroll_trigger_window_ms==settings_fast_trigger_values[i] &&
              !g_system_config.fast_scroll_enabled,
              "Every Trigger Time choice must work while Fast Scroll is disabled");
        for(int j=0;j<3;j++)
            check(!strcmp(lv_label_get_text(settings_fast_trigger_checks[j]),j==i ? ICON_CHECK : ""),
                  "Only the chosen Trigger Time must have a checkmark");
    }
    ui_handle_key(UI_KEY_DOWN);ui_handle_key(UI_KEY_ENTER);
    check(g_system_config.fast_scroll_pages==1 && !g_system_config.fast_scroll_enabled,
          "A threshold can be changed while disabled without re-enabling Fast Scroll");
    ui_handle_key_repeat(UI_KEY_UP,4);ui_handle_key(UI_KEY_ENTER);
    check(g_system_config.fast_scroll_enabled && g_system_config.fast_scroll_pages==1 &&
          !strcmp(lv_label_get_text(settings_fast_scroll_enabled_value),"Enabled"),
          "Re-enabling must retain the chosen threshold");
    check(g_system_config.fast_scroll_trigger_window_ms==1200,"Re-enabling must preserve Trigger Time");
    /* Visit individual options without this navigation becoming a fast spin. */
    for(int i=0;i<9;i++) { pump(160);ui_handle_key(UI_KEY_DOWN); }
    for(int i=0;i<4;i++) {
        if(i) ui_handle_key(UI_KEY_DOWN);
        ui_handle_key(UI_KEY_ENTER);
        check(g_system_config.fast_scroll_hold_ms==settings_fast_hold_values[i],
              "Each Hold Time choice must update the persisted setting");
    }
    ui_handle_key(UI_KEY_BACK);ui_select_screen(SCR_NOW_PLAYING);pump(200);
    check(!g_fast_scroll.active && !g_fast_context,"Changing tabs must discard velocity history");
}

static void cursor_band_views(void)
{
    const screen_id_t views[]={SCR_ALBUMS,SCR_ARTISTS,SCR_SONGS,SCR_PLAYLISTS,SCR_SETTINGS};
    g_system_config.fast_scroll_enabled=true;g_system_config.fast_scroll_pages=2;
    g_system_config.fast_scroll_trigger_window_ms=550;g_system_config.fast_scroll_hold_ms=300;
    for(unsigned i=0;i<sizeof(views)/sizeof(views[0]);i++) {
        ui_select_screen(views[i]);pump(600);
        lv_obj_t *context;int rows=ui_fast_scroll_rows(&context);
        check(rows>0 && context,"Every list fixture must allow fast scrolling");
        uint32_t now=lv_tick_get();
        ui_handle_key_repeat_timed(UI_KEY_DOWN,rows*2,now-500,now);pump(40);
        check(g_fast_scroll.active,"Every list must enter fast scrolling");check_cursor_band(true);
        int physical=rows*2;ui_fast_scroll_reset();uint32_t tick=lv_tick_get();
        for(int n=0;n<physical;n++){
            ui_handle_key(UI_KEY_ACTIVITY);
            ui_handle_key_repeat_timed(UI_KEY_DOWN,1,tick,tick);tick+=10;
        }
        check(g_fast_scroll.active,"Linked activity preserves acceleration on every list");
        check_cursor_band(true);
        ui_fast_scroll_reset();check_cursor_band(false);
    }
    ui_select_screen(SCR_NOW_PLAYING);pump(200);
}

static void settings_widget_lifetime(void)
{
    settings_selected=SETTING_FAST_SCROLL;settings_detail_open=false;
    ui_select_screen(SCR_SETTINGS);pump(600);
    check(settings_fast_scroll_enabled_value!=NULL,"Fast Scroll preview must be rendered before leaving Settings");
    ui_select_screen(SCR_ALBUMS);
    check(settings_fast_scroll_enabled_value==NULL && settings_fast_trigger_checks[0]==NULL && settings_fast_scroll_checks[0]==NULL &&
          settings_fast_hold_checks[0]==NULL,"Leaving Settings must drop references to deleted detail widgets");
    pump(600);ui_select_screen(SCR_SETTINGS);pump(70);
    check(!g_page_loading && settings_detail_dirty && !settings_fast_scroll_enabled_value,
          "Returning to Settings must wait for its deferred detail render with no stale labels");
    settings_tick();pump(400);
    check(settings_fast_scroll_enabled_value && !settings_detail_dirty &&
          !strcmp(lv_label_get_text(settings_fast_scroll_enabled_value),"Enabled"),
          "Fast Scroll preview must refresh normally after returning to Settings");
    lv_obj_send_event(lv_obj_get_parent(settings_fast_trigger_checks[1]),LV_EVENT_CLICKED,NULL);
    pump(100);
    check(g_system_config.fast_scroll_trigger_window_ms==800 &&
          !strcmp(lv_label_get_text(settings_fast_trigger_checks[1]),ICON_CHECK),
          "The preview Trigger Time choices must also work through touch callbacks");
}

static void rapid_tab_navigation(void)
{
    ui_select_screen(SCR_NOW_PLAYING);pump(600);
    for(int round=0;round<12;round++) {
        for(int step=0;step<6;step++) {
            ui_handle_key(UI_KEY_SCREEN_NEXT);pump(40+(round%4)*50);
        }
        for(int step=0;step<8;step++) {
            ui_handle_key(UI_KEY_SCREEN_PREV);pump(40+(round%4)*50);
        }
        check(g_current_screen==SCR_NOW_PLAYING,"Repeated previous-tab input must return to Now Playing");
        for(int step=0;step<6;step++) {
            ui_handle_key(UI_KEY_SCREEN_PREV);pump(30+step*20);
        }
        ui_handle_key(UI_KEY_BACK);pump(200);
    }
    ui_select_screen(SCR_ALBUMS);pump(600);
    check(!g_page_loading && albums_left_list && albums_right_content,
          "Albums must remain usable after rapid tab and queue transitions");
    ui_select_screen(SCR_NOW_PLAYING);pump(600);
}

static ui_system_config_t saved_order;
static int order_saves;
static void save_order(const ui_system_config_t *config)
{ saved_order=*config; order_saves++; }

static void sort_order_settings(void)
{
    int original_field=g_sort_field;
    bool original_desc=g_sort_desc;
    ui_set_system_config_changed_cb(save_order);
    ui_select_screen(SCR_SETTINGS); pump(600);
    settings_open((void *)(intptr_t)SETTING_SORT_ORDER); pump(200);
    check(g_focus_count==4 && g_focus_index==0 && settings_detail_sort_value && settings_detail_order_value &&
          settings_detail_song_sort_value && settings_detail_song_order_value,
          "Sort Order must expose independent album and song controls to the knob");
    check(!strcmp(settings_names[SETTING_SORT_ORDER],"Sort Order") &&
          !strcmp(settings_sort_field_name(SORT_FIELD_ADD_DATE),"Date Added"),
          "Sort Order must use the standard Date Added label");
    check(!strcmp(lv_label_get_text(first_label(lv_obj_get_parent(settings_detail_sort_value))),"Albums: Sort By") &&
          !strcmp(lv_label_get_text(first_label(lv_obj_get_parent(settings_detail_order_value))),"Albums: Order") &&
          !strcmp(lv_label_get_text(first_label(lv_obj_get_parent(settings_detail_song_sort_value))),"Songs: Sort By") &&
          !strcmp(lv_label_get_text(first_label(lv_obj_get_parent(settings_detail_song_order_value))),"Songs: Order"),
          "Sort Order must label all four independent controls");
    for(int i=0;i<SORT_FIELD_COUNT;i++) {
        ui_handle_key(UI_KEY_ENTER); pump(100);
        check(g_sort_field==(original_field+i+1)%SORT_FIELD_COUNT &&
              saved_order.sort_field==g_sort_field && order_saves==i+1,
              "Albums Sort By must cycle through Date Added, Name and Year and persist each choice");
    }
    ui_handle_key(UI_KEY_DOWN); ui_handle_key(UI_KEY_ENTER); pump(100);
    check(g_sort_desc!=original_desc && saved_order.sort_desc==g_sort_desc &&
          !strcmp(lv_label_get_text(settings_detail_order_value),settings_sort_order_text()),
          "Order must toggle and persist Ascending or Descending");
    int song_field=g_song_sort_field;
    bool song_desc=g_song_sort_desc;
    ui_handle_key(UI_KEY_DOWN);
    for(int i=0;i<SORT_FIELD_COUNT;i++) {
        ui_handle_key(UI_KEY_ENTER);pump(100);
        check(g_song_sort_field==(song_field+i+1)%SORT_FIELD_COUNT &&
              saved_order.song_sort_field==g_song_sort_field && g_sort_field==original_field &&
              g_sort_desc!=original_desc,
              "Songs Sort By must persist independently of Albums");
    }
    ui_handle_key(UI_KEY_DOWN);ui_handle_key(UI_KEY_ENTER);pump(100);
    check(g_song_sort_desc!=song_desc && saved_order.song_sort_desc==g_song_sort_desc &&
          saved_order.sort_desc==g_sort_desc,
          "Songs Order must persist independently of Albums");
    ui_select_screen(SCR_ALBUMS);
    check(!settings_detail_sort_value && !settings_detail_order_value &&
          !settings_detail_song_sort_value && !settings_detail_song_order_value,
          "Leaving Settings must clear the deleted order controls");
    pump(600); ui_select_screen(SCR_SETTINGS); pump(600);
    check(settings_detail_order_value && settings_detail_sort_value &&
          settings_detail_song_sort_value && settings_detail_song_order_value,
          "Returning to Settings must rebuild the saved order controls");
    lv_obj_send_event(lv_obj_get_parent(settings_detail_order_value),LV_EVENT_CLICKED,NULL); pump(100);
    check(g_sort_desc==original_desc && saved_order.sort_desc==original_desc,
          "Order must also work through the touch preview");
    lv_obj_send_event(lv_obj_get_parent(settings_detail_song_order_value),LV_EVENT_CLICKED,NULL);pump(100);
    check(g_song_sort_desc==song_desc && saved_order.song_sort_desc==song_desc,
          "Songs Order must also work through the touch preview");
    ui_set_system_config_changed_cb(NULL);
    ui_select_screen(SCR_NOW_PLAYING); pump(600);
}

static void live_album_focus(void)
{
    ui_select_screen(SCR_NOW_PLAYING);pump(100);
    albums_visited=false;albums_selected=0;albums_left_scroll_y=0;
    g_sort_field=0;g_sort_desc=true;
    player_library_reset();
    for(int i=1;i<=2;i++) {
        player_media_item_t item={.pid=(uint64_t)i,.album_pid=1000u+i,
            .fields=(1u<<0)|(1u<<1)|(1u<<5)|(1u<<6)|(1u<<12)|(1u<<14)};
        snprintf(item.title,sizeof(item.title),"Song %d",i);
        snprintf(item.album,sizeof(item.album),"%c album",'A'+i-1);
        strcpy(item.artist,"Artist");strcpy(item.album_artist,"Artist");
        player_library_update(&item);
    }
    player_library_complete();pump(200);
    check(albums_selected==0,"An unvisited Albums page must keep its initial top selection");
    ui_select_screen(SCR_ALBUMS);pump(200);albums_select(1);pump(200);
    player_album_t selected;check(player_album_get(albums_selected,&selected),"Selected fixture album exists");
    uint64_t selected_id=selected.id;
    ui_select_screen(SCR_NOW_PLAYING);pump(100);
    player_media_item_t item={.pid=3,.album_pid=1003,
        .fields=(1u<<0)|(1u<<1)|(1u<<5)|(1u<<6)|(1u<<12)|(1u<<14)};
    strcpy(item.title,"New song");strcpy(item.album,"New album");
    strcpy(item.artist,"Artist");strcpy(item.album_artist,"Artist");
    player_library_update(&item);pump(200);
    check(player_album_get(albums_selected,&selected) && selected.id==selected_id &&
          albums_selected==2 && albums_left_scroll_y>=ALBUMS_STRIDE,
          "An offscreen catalog update must preserve the album PID and scroll position");
    ui_select_screen(SCR_ALBUMS);pump(200);
    albums_open((void*)(intptr_t)albums_selected);pump(200);
    for(int i=0;i<g_focus_count;i++) if(g_focus[i].activate==albums_track_action) {
        focus_select(i);break;
    }
    item.pid=4;item.album_pid=1004;strcpy(item.album,"Newest album");
    player_library_update(&item);pump(200);
    check(player_album_get(albums_selected,&selected) && selected.id==selected_id &&
          g_focus_index>=0 && g_focus[g_focus_index].activate==albums_track_action,
          "Live updates must preserve the album detail track focus");
}

static void live_song_focus(void)
{
    g_song_sort_field=SORT_FIELD_NAME;g_song_sort_desc=false;
    ui_select_screen(SCR_SONGS);pump(300);songs_select(0);pump(100);
    player_track_t selected;check(player_song_get(songs_selected,&selected),"Selected fixture song exists");
    uint64_t selected_id=selected.id;
    ui_select_screen(SCR_SETTINGS);pump(200);
    settings_sort_order_action((void*)(intptr_t)1);pump(200);
    ui_select_screen(SCR_SONGS);pump(300);
    check(player_song_get(songs_selected,&selected) && selected.id==selected_id &&
          songs_selected!=0 && songs_selected_pid==selected_id,
          "Changing song order offscreen must retain the selected PID");
    player_media_item_t update={.pid=selected_id,.fields=1u<<1};
    strcpy(update.title,"ZZZZ renamed song");player_library_update(&update);pump(300);
    check(player_song_get(songs_selected,&selected) && selected.id==selected_id &&
          songs_selected==0 && songs_selected_pid==selected_id,
          "A live song update must rebuild the sorted view and preserve selection");
}


static void album_pane_retention(void)
{
    ui_select_screen(SCR_ALBUMS);pump(500);albums_select(1);pump(400);
    lv_obj_t *left=albums_left_list;
    lv_obj_t *rows[ALBUMS_POOL];const void *covers[ALBUMS_POOL];
    for(int i=0;i<ALBUMS_POOL;i++) {
        rows[i]=albums_rows[i];covers[i]=lv_image_get_src(albums_row_covers[i]);
    }
    for(int index=2;index<=3;index++) {
        albums_open((void*)(intptr_t)index);pump(300);
        check(albums_left_list==left,"Opening another album must retain the left viewport");
        for(int i=0;i<ALBUMS_POOL;i++) {
            check(albums_rows[i]==rows[i],"Opening album details must retain pooled rows");
            check(lv_image_get_src(albums_row_covers[i])==covers[i],"Opening album details must retain every loaded list cover");
        }
        check(g_focus_index>=0 && g_focus[g_focus_index].activate==albums_play_all_action,
              "The retained list must still open the new album's Play control");
    }
    ui_handle_key(UI_KEY_BACK);pump(300);
    check(albums_left_list==left && g_focus_count==0,"Back returns to the same virtual album viewport");
    for(int i=0;i<ALBUMS_POOL;i++)
        check(lv_image_get_src(albums_row_covers[i])==covers[i],"Returning to album navigation must retain list artwork");
}

static pagination_t *pager_for(lv_obj_t *list)
{
    for(unsigned i=0;i<sizeof(g_pagination)/sizeof(g_pagination[0]);i++)
        if(g_pagination[i].list==list)return &g_pagination[i];
    check(false,"Each list must own a pagination overlay");return NULL;
}
static void tap_pager(lv_obj_t *button)
{
    lv_area_t a;lv_obj_get_coords(button,&a);
    lv_point_t hit={(a.x1+a.x2)/2,(a.y1+a.y2)/2};
    pointer(hit.x,hit.y,true);
    pointer(hit.x,hit.y,false);pump(350);
}
static int pagination_saved;
static void pagination_save(const ui_system_config_t *cfg)
{ pagination_saved=cfg->pagination_size; }

static void pagination_views(void)
{
    touch=lv_indev_create();lv_indev_set_type(touch,LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(touch,read_touch);ui_set_touch_enabled(true);ui_attach_touch(touch);
    check(!g_system_config.pagination_size,"Pagination defaults to Off");
    g_system_config.pagination_size=64;
    ui_handle_key(UI_KEY_UP);
    albums_selected=0;albums_left_scroll_y=0;
    ui_select_screen(SCR_ALBUMS);pump(400);
    check(lv_obj_has_flag(pager_for(albums_left_list)->controls,LV_OBJ_FLAG_HIDDEN),
          "Keyboard navigation must keep pagination hidden");
    /* A real touch switches the existing view into touch navigation. */
    pointer(54,35,true);pointer(54,35,false);pump(100);
    pagination_t *left=pager_for(albums_left_list);
    check(lv_obj_is_visible(left->controls),"On touch must reveal list pagination");
    check(lv_obj_get_width(left->up)==64 && lv_obj_get_height(left->up)==64 &&
          lv_obj_get_width(left->down)==64 && lv_obj_get_height(left->down)==64,
          "On touch 64 must create two 64 by 64 touch targets");
    check(lv_obj_get_style_opa(left->controls,0)==89,"Pagination is 65 percent transparent");
    lv_obj_scroll_to_y(albums_left_list,0,LV_ANIM_OFF);pump(150);
    if(getenv("TAB5_PAGINATION_FRAME")) {
        FILE *out=fopen(getenv("TAB5_PAGINATION_FRAME"),"wb");
        check(out!=NULL,"Pagination frame output opens");
        fwrite(frame,1,sizeof(frame),out);fclose(out);
    }
    int before=lv_obj_get_scroll_y(albums_left_list);
    int selected=albums_selected;
    lv_area_t button_bounds,arrow_before,arrow_after;
    lv_obj_get_coords(left->down,&button_bounds);
    lv_obj_t *arrow=lv_obj_get_child(left->down,0);
    lv_obj_get_coords(arrow,&arrow_before);
    int clicks=click_cues;
    pointer((button_bounds.x1+button_bounds.x2)/2,(button_bounds.y1+button_bounds.y2)/2,true);
    check(lv_obj_has_state(left->down,LV_STATE_USER_2) && lv_obj_has_state(arrow,LV_STATE_USER_2),
          "Pagination buttons and arrows must share the normal touch press feedback");
    lv_obj_get_coords(arrow,&arrow_after);
    check(lv_area_is_equal(&arrow_before,&arrow_after) && lv_obj_get_style_opa(left->controls,0)==89,
          "Pagination press feedback preserves geometry and transparency");
    check(lv_color_eq(lv_obj_get_style_text_color(arrow,0),lv_color_hex(g_theme->on_selection)),
          "Pressed pagination arrows must use the normal readable feedback color");
    lv_indev_wait_release(touch);pointer(touch_point.x,touch_point.y,false);
    check(!lv_obj_has_state(left->down,LV_STATE_USER_2) && !lv_obj_has_state(arrow,LV_STATE_USER_2) && click_cues==clicks,
          "Cancelling pagination must clear feedback without a click tone");
    check(lv_obj_has_state(left->up,LV_STATE_DISABLED),"Page up is disabled at the top");
    tap_pager(left->down);
    check(lv_obj_get_scroll_y(albums_left_list)>before,"Page down must scroll the virtual album list");
    check(albums_selected==selected,"Paging must not activate or change album selection");
    lv_area_t a,b;lv_obj_get_coords(left->controls,&a);lv_obj_get_coords(albums_left_list,&b);
    check(a.x2==b.x2-lv_obj_get_style_pad_right(albums_left_list,0) &&
          a.y2==b.y2-lv_obj_get_style_pad_bottom(albums_left_list,0),
          "Pagination follows the list's existing right and bottom margins");
    tap_pager(left->up);
    check(lv_obj_get_scroll_y(albums_left_list)==0,"Page up must return one viewport without overscrolling");
    int saved_mode=g_system_config.sound_mode;
    bool saved_sound=g_system_config.sound_enabled;
    for(int mode=0;mode<=2;mode++) {
        g_system_config.sound_mode=mode;g_system_config.sound_enabled=mode!=0;
        clicks=click_cues;tap_pager(left->down);tap_pager(left->up);
        check(click_cues==clicks+(mode?2:0),"Each page tap emits exactly one enabled tone; None remains silent");
        clicks=click_cues;tap_pager(left->up);
        check(click_cues==clicks,"Disabled pagination buttons must not produce feedback tones");
    }
    g_system_config.sound_mode=saved_mode;g_system_config.sound_enabled=saved_sound;
    lv_obj_scroll_to_y(albums_left_list,1000000,LV_ANIM_OFF);pump(150);
    check(lv_obj_has_state(left->down,LV_STATE_DISABLED),"Page down is disabled at the final viewport");
    int bottom=lv_obj_get_scroll_y(albums_left_list);
    tap_pager(left->up);
    check(lv_obj_get_scroll_y(albums_left_list)<bottom,"Page up leaves the final viewport");
    ui_handle_key(UI_KEY_DOWN);pump(100);
    check(!lv_obj_is_visible(left->controls),"Physical navigation hides pagination");

    lv_obj_t *retained_list=albums_left_list;
    ui_system_config_t resized=g_system_config;resized.pagination_size=96;
    ui_set_system_config(&resized);pump(100);
    check(albums_left_list==retained_list && lv_obj_get_width(left->up)==96 &&
          lv_obj_get_height(left->up)==96 && lv_obj_get_width(left->down)==96 &&
          lv_obj_get_height(left->down)==96,"Size changes resize existing buttons without rebuilding the list");
    check(!lv_obj_is_visible(left->controls),"Changing size preserves keyboard navigation mode");
    screen_id_t pages[]={SCR_ALBUMS,SCR_ARTISTS,SCR_SONGS,SCR_PLAYLISTS,SCR_BLUETOOTH,SCR_SETTINGS};
    for(unsigned n=0;n<sizeof(pages)/sizeof(pages[0]);n++) {
        ui_select_screen(pages[n]);pump(500);
        pointer(54,35,true);pointer(54,35,false);pump(100);
        int count=0;
        for(unsigned i=0;i<sizeof(g_pagination)/sizeof(g_pagination[0]);i++) {
            pagination_t *p=&g_pagination[i];if(!p->list)continue;
            count++;
            check(lv_obj_is_valid(p->list) && lv_obj_is_valid(p->controls),"Page changes must retire all deleted lists");
            check(lv_obj_is_visible(p->controls),"Every visible list has touch pagination");
            check(lv_obj_get_width(p->up)==96 && lv_obj_get_height(p->up)==96 &&
                  lv_obj_get_y(p->down)-lv_obj_get_y(p->up)==104,
                  "On touch 96 uses 96-pixel buttons and preserves the 8-pixel gap");
            int others[12];
            for(int j=0;j<12;j++)others[j]=g_pagination[j].list?lv_obj_get_scroll_y(g_pagination[j].list):0;
            bool scrollable=lv_obj_get_scroll_bottom(p->list)>0;
            int old=lv_obj_get_scroll_y(p->list);
            if(scrollable) {
                tap_pager(p->down);
                check(lv_obj_get_scroll_y(p->list)>old,"Page button must scroll its own viewport");
                for(int j=0;j<12;j++)if(j!=(int)i && g_pagination[j].list)
                    check(lv_obj_get_scroll_y(g_pagination[j].list)==others[j],"Paging one pane must preserve every other viewport");
                tap_pager(p->up);
            }
        }
        check(count==(pages[n]==SCR_SONGS || pages[n]==SCR_BLUETOOTH ? 1:2),
              "Each page must register exactly one overlay per list");
    }
    ui_select_screen(SCR_NOW_PLAYING);pump(400);np_toggle_queue();pump(400);
    pointer(54,35,true);pointer(54,35,false);pump(100);
    check(np_queue_list && lv_obj_is_visible(pager_for(np_queue_list)->controls),
          "Up Next also exposes its own touch pagination");
    np_toggle_queue();pump(200);
    ui_select_screen(SCR_SETTINGS);pump(400);
    settings_open((void *)(intptr_t)SETTING_SCREEN);pump(300);
    lv_obj_t *first=lv_obj_get_child(settings_right_content,0);
    check(!strcmp(lv_label_get_text(lv_obj_get_child(first,0)),"Pagination"),
          "Pagination is the first item in Screen settings");
    ui_set_system_config_changed_cb(pagination_save);
    settings_pagination_action(NULL);pump(100);
    check(pagination_saved==0 && !strcmp(lv_label_get_text(settings_pagination_value),"Off"),
          "Off must update its value and persist through the settings callback");
    for(int i=0;i<12;i++)if(g_pagination[i].list)
        check(!lv_obj_is_visible(g_pagination[i].controls),"Off removes pagination from both panes");
    settings_pagination_action(NULL);pump(100);
    check(pagination_saved==64 && !strcmp(lv_label_get_text(settings_pagination_value),"On touch 64"),
          "On touch 64 must persist and use the normal menu value style");
    settings_pagination_action(NULL);pump(100);
    check(pagination_saved==96 && !strcmp(lv_label_get_text(settings_pagination_value),"On touch 96"),
          "The third option must persist the 96-pixel size");
    ui_set_touch_enabled(false);pump(100);
    for(int i=0;i<12;i++)if(g_pagination[i].list)
        check(!lv_obj_is_visible(g_pagination[i].controls),"Disabling touch hides pagination");
    ui_set_touch_enabled(true);g_system_config.pagination_size=0;pagination_refresh();
    ui_set_system_config_changed_cb(NULL);ui_attach_touch(NULL);lv_indev_delete(touch);touch=NULL;
}

static int saved_volume_steps;
static void volume_step_saved(const ui_system_config_t *config)
{ saved_volume_steps=config->volume_steps; }
static void volume_step_settings(void)
{
    ui_select_screen(SCR_SETTINGS);pump(400);
    settings_open((void*)(intptr_t)SETTING_VOLUME_STEP);pump(300);
    check(g_focus_count==4 && lv_obj_get_child_count(settings_right_content)==4,
          "Volume Scale settings must contain all four presets");
    check(g_system_config.volume_steps==64 && player_volume_steps()==64,
          "Missing volume scale configuration defaults to 1/64 Squared Curve");
    ui_set_system_config_changed_cb(volume_step_saved);
    ui_handle_key(UI_KEY_ENTER);pump(200);
    check(saved_volume_steps==64 && player_volume_steps()==64 &&
          !strcmp(lv_label_get_text(settings_volume_step_checks[1]),ICON_CHECK),
          "Keyboard selection must apply and persist 1/64");
    ui_system_config_t config=g_system_config;config.volume_steps=16;
    ui_set_system_config(&config);pump(600);
    check(player_volume_steps()==16 && !strcmp(lv_label_get_text(settings_volume_step_checks[0]),ICON_CHECK),
          "Web configuration changes must update the volume grid and checked option");
    touch=lv_indev_create();lv_indev_set_type(touch,LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(touch,read_touch);ui_set_touch_enabled(true);ui_attach_touch(touch);
    tap_pager(lv_obj_get_child(settings_right_content,1));
    check(saved_volume_steps==64 && player_volume_steps()==64,"Touch must select the same 1/64 grid");
    tap_pager(lv_obj_get_child(settings_right_content,2));
    check(saved_volume_steps==2 && player_volume_steps()==2,"Touch selects 2% Integer");
    tap_pager(lv_obj_get_child(settings_right_content,3));
    check(saved_volume_steps==5 && player_volume_steps()==5,"Touch selects 5% Integer");
    ui_attach_touch(NULL);lv_indev_delete(touch);touch=NULL;
    settings_volume_step_action((void*)(intptr_t)64);ui_set_system_config_changed_cb(NULL);
}

static bool playlist_view_deleted;
static void playlist_view_delete_cb(lv_event_t *event)
{ LV_UNUSED(event);playlist_view_deleted=true; }

static void playlist_updates_retain_album_covers(void)
{
    ui_select_screen(SCR_ALBUMS);pump(400);
    lv_obj_t *left=albums_left_list;
    player_track_t first,second;
    check(player_song_get(0,&first) && player_song_get(1,&second),"Playlist update fixture has tracks");
    player_library_playlists_reset();
    player_library_playlist_item(0,"Remote mix",0);
    player_library_playlist_item(0,NULL,first.id);pump(300);
    check(albums_left_list==left,"Playlist-only updates must not rebuild album rows or covers");
    ui_select_screen(SCR_PLAYLISTS);pump(400);
    playlist_view_deleted=false;
    lv_obj_add_event_cb(playlists_left_list,playlist_view_delete_cb,LV_EVENT_DELETE,NULL);
    player_library_playlist_item(0,NULL,second.id);pump(300);
    player_playlist_t playlist;
    check(player_playlist_get(1,&playlist) && playlist.track_count==2,
          "Playlist-only updates must remain visible without catalog invalidation");
    check(playlist_view_deleted,"The visible Playlists page refreshes when server membership changes");
}

static void remote_play_controls(const char *heading,focus_activate_fn play,focus_activate_fn shuffle)
{
    ui_navigation_t nav;bool saw_play=false,saw_shuffle=false;
    for(int i=0;i<g_focus_count;i++)if(g_focus[i].activate==play || g_focus[i].activate==shuffle) {
        focus_select(i);const int before=g_focus_index;ui_navigation_snapshot(&nav);
        bool is_play=g_focus[i].activate==play;
        check((nav.flags&8) && !strcmp(nav.focus,is_play?"Play":"Shuffle"),"Icon-only controls have semantic remote labels");
        check(!nav.count && !nav.index,"Playback controls have no song counter");
        check(!strcmp(nav.heading,heading),"Detail feedback uses the album, artist or playlist heading");
        check(before==g_focus_index,"Feedback never changes the selected control");
        if(is_play)saw_play=true;else saw_shuffle=true;
    }
    check(saw_play && saw_shuffle,"Detail fixture exposes both Play and Shuffle");
}

static void remote_navigation_snapshot(void)
{
    ui_navigation_t nav;
    ui_select_screen(SCR_ALBUMS);pump(600);albums_select(1);pump(60);
    player_album_t album;check(player_album_get(albums_selected,&album),"Album fixture exists");
    int focus=g_focus_index;ui_navigation_snapshot(&nav);
    check(nav.view==SCR_ALBUMS && (nav.flags&8) && !strcmp(nav.focus,album.title),"Remote focus reads virtual album selection");
    check(nav.index==(uint32_t)albums_selected && nav.count==(uint32_t)player_album_count(),"Remote album position uses library indices");
    check(focus==g_focus_index,"Reading remote focus never moves selection");
    ui_handle_key(UI_KEY_ENTER);pump(600);
    remote_play_controls(album.title,albums_play_all_action,albums_shuffle_action);
    for(int i=0;i<g_focus_count;i++)if(g_focus[i].activate==albums_track_action){
        focus_select(i);ui_navigation_snapshot(&nav);player_track_t selected;
        check(player_album_track_get(album.id,(int)(intptr_t)g_focus[i].user,&selected) && !strcmp(nav.focus,selected.title),"Album track focus reports its title, not the leading track number");
        check(nav.index==0 && nav.count==album.track_count,"First album song is 1/n without the Play and Shuffle controls");
        check(!strcmp(nav.heading,album.title),"Album name stays in the heading for tracks");
        break;
    }
    ui_handle_key(UI_KEY_BACK);pump(100);
    ui_handle_key(UI_KEY_CONTEXT);pump(100);ui_navigation_snapshot(&nav);
    check((nav.flags&1) && !strcmp(nav.focus,lv_label_get_text(g_ctx_menu_items[g_ctx_menu_focus].label)),"Context menu reports its focused command");
    ui_handle_key(UI_KEY_DOWN);ui_navigation_snapshot(&nav);
    check(nav.index==(uint32_t)g_ctx_menu_focus,"Context menu movement reaches the snapshot");
    ui_handle_key(UI_KEY_BACK);
    ui_select_screen(SCR_SONGS);pump(600);songs_select(2);pump(60);
    player_track_t track;check(player_song_get(songs_selected,&track),"Song fixture exists");ui_navigation_snapshot(&nav);
    check(nav.view==SCR_SONGS && !strcmp(nav.focus,track.title),"Virtual song focus reports its metadata");
    ui_select_screen(SCR_ARTISTS);pump(600);
    int artist_fixture=-1;
    for(int i=0;i<player_artist_count() && artist_fixture<0;i++) {
        player_artist_t a;player_track_t first;player_album_t album;
        if(!player_artist_get(i,&a) || !player_artist_song_get(a.id,0,&first))continue;
        for(int j=0;j<player_album_count();j++)if(player_album_get(j,&album) && album.id==first.album_id){artist_fixture=i;break;}
    }
    check(artist_fixture>=0,"Artist navigation fixture has a visible album and song");artists_select(artist_fixture);pump(60);
    player_artist_t artist;check(player_artist_get(artists_selected,&artist),"Artist fixture exists");ui_navigation_snapshot(&nav);
    check(!strcmp(nav.focus,artist.name),"Virtual artist focus reports its metadata");
    ui_handle_key(UI_KEY_ENTER);pump(600);
    remote_play_controls(artist.name,artists_play_all_action,artists_shuffle_action);
    bool artist_track=false,artist_album=false;
    for(int i=0;i<g_focus_count;i++) {
        if(g_focus[i].activate==artists_album_action && !artist_album) {
            focus_select(i);ui_navigation_snapshot(&nav);artist_album=true;
            check(nav.index==0 && nav.count==artist.album_count,"Artist album positions exclude controls and songs");
        }
        if(g_focus[i].activate==artists_track_action && !artist_track) {
            focus_select(i);ui_navigation_snapshot(&nav);artist_track=true;
            player_track_t selected;check(player_artist_song_get(artist.id,(int)(intptr_t)g_focus[i].user,&selected),"Artist song exists");
            check(!strcmp(nav.focus,selected.title) && !strcmp(nav.heading,artist.name),"Artist track has metadata and the artist heading");
            check(nav.index==(uint32_t)(intptr_t)g_focus[i].user && nav.count==(uint32_t)player_artist_song_count(artist.id),"Artist song positions exclude album headers and controls");
        }
    }
    check(artist_track && artist_album,"Artist fixture includes albums and songs");
    ui_select_screen(SCR_PLAYLISTS);pump(600);ui_navigation_snapshot(&nav);
    check(nav.view==SCR_PLAYLISTS && (nav.flags&8) && nav.focus[0],"Playlist focus extracts readable row text");
    if(g_focus_index>=0 && g_focus[g_focus_index].activate==playlists_open){
        player_playlist_t playlist;check(player_playlist_get((int)(intptr_t)g_focus[g_focus_index].user,&playlist) && !strcmp(nav.focus,playlist.name),"Playlist focus reports the metadata name");
    }
    playlists_open((void*)(intptr_t)1);pump(300);
    player_playlist_t playlist;check(player_playlist_get(1,&playlist) && playlist.track_count,"Playlist detail fixture has songs");
    remote_play_controls(playlist.name,playlists_play_action,playlists_shuffle_action);
    for(int i=0;i<g_focus_count;i++)if(g_focus[i].activate==playlists_track_action) {
        focus_select(i);ui_navigation_snapshot(&nav);
        check(nav.index==0 && nav.count==playlist.track_count,"First playlist song is 1/n without playback controls");
        check(!strcmp(nav.heading,playlist.name),"Playlist heading uses its name");break;
    }
    ui_select_screen(SCR_SETTINGS);pump(600);ui_navigation_snapshot(&nav);
    check(nav.view==SCR_SETTINGS && (nav.flags&8) && nav.focus[0],"Settings focus excludes icon-only labels");
    check(!strcmp(nav.heading,"Settings"),"Settings root has a semantic heading");
    settings_open((void*)(intptr_t)SETTING_KEY_FEEDBACK);pump(200);focus_select(0);ui_navigation_snapshot(&nav);
    char expected[32];snprintf(expected,sizeof(expected),"Volume: %d%%",g_system_config.volume);
    check(!strcmp(nav.heading,"Key Feedback") && !strcmp(nav.focus,expected),"Settings feedback includes the actual row name and current value");
    check(nav.index==0 && nav.count==(uint32_t)g_focus_count,"Settings position counts actionable rows");
    ui_handle_key(UI_KEY_ENTER);ui_handle_key(UI_KEY_UP);ui_navigation_snapshot(&nav);
    snprintf(expected,sizeof(expected),"Volume: %d%%",g_system_config.volume);
    check((nav.flags&2) && !strcmp(nav.focus,expected),"Settings feedback updates while editing");ui_handle_key(UI_KEY_BACK);
    settings_open((void*)(intptr_t)SETTING_DATETIME);pump(200);ui_navigation_snapshot(&nav);
    check(strcmp(nav.focus,"Increase") && strcmp(nav.focus,"Decrease"),"Clock format row retains its own setting and value");
    for(int i=0;i<g_focus_count;i++)if(g_focus[i].obj==settings_time_year_value){focus_select(i);break;}
    ui_navigation_snapshot(&nav);
    check(!strcmp(nav.heading,"Set time") && !strncmp(nav.focus,"Year: ",6),"Clock editor includes its field label");
    ui_handle_key(UI_KEY_ENTER);ui_navigation_snapshot(&nav);char before_blink[96];strcpy(before_blink,nav.focus);
    settings_time_blink_on=false;settings_time_apply_blink();ui_navigation_snapshot(&nav);
    check((nav.flags&2) && !strcmp(nav.focus,before_blink),"Clock editing blink does not blank the remote value");
    for(int i=0;i<g_focus_count;i++)if(g_focus[i].obj==settings_time_up_button) {
        focus_select(i);ui_navigation_snapshot(&nav);check(!strcmp(nav.focus,"Increase") && !nav.count,"Clock arrow controls have readable names and no field counter");break;
    }
    g_page_loading=true;ui_navigation_snapshot(&nav);g_page_loading=false;
    check((nav.flags&4) && !(nav.flags&8) && !nav.focus[0],"Page loading clears old remote focus");
    char clipped[6];navigation_copy(clipped,sizeof(clipped),"日本語");
    check(!strcmp(clipped,"日"),"Navigation truncation preserves UTF-8 boundaries");
}

static void albums_shuffle_all_action(void)
{
    ui_select_screen(SCR_ALBUMS);pump(300);albums_select(0);ui_handle_key(UI_KEY_UP);
    ui_navigation_t nav;ui_navigation_snapshot(&nav);
    check(!strcmp(nav.focus,"Shuffle")&&!nav.count,"Shuffle All has a focused action without a song count");
    ui_handle_key(UI_KEY_ENTER);player_snapshot_t snapshot;player_get_snapshot(&snapshot);
    check(snapshot.status==PLAYER_PLAYING&&snapshot.shuffle&&player_queue_count()==player_song_count(),"Shuffle All Enter starts a shuffled queue covering the library");
}

static void playlist_context_picker(void)
{
    player_track_t selected,current,added;player_snapshot_t before,after;player_playlist_t list;
    check(player_song_get(1,&selected) && player_song_get(0,&current),"Playlist picker fixture has two songs");
    player_library_playlists_reset();player_library_playlist_item(0,"Mix A",0);player_library_playlist_item(0,NULL,current.id);
    player_library_playlist_item(1,"Mix B",0);pump(300);
    ui_select_screen(SCR_SONGS);pump(500);songs_select(1);pump(50);
    player_get_snapshot(&before);int queue_before=player_queue_count();
    int selected_row=songs_selected;menu_open_key(CTX_TRACK,selected.id);
    check(menu_item_named("Add to Playlist")>=0,"Song context includes Add to Playlist");
    menu_choose("Add to Playlist");check(g_ctx_playlist_picker && g_ctx_playlist_count==2,"Destination picker uses existing regular playlists");
    check(menu_item_named("Stared")==-1 && menu_item_named("Starred")==-1,"Reserved star playlist is excluded from destinations");
    ui_navigation_t nav;ui_navigation_snapshot(&nav);
    check((nav.flags&1) && !strcmp(nav.focus,"Mix A"),"ColoPlay's modal navigation reports the focused destination");
    ui_handle_key(UI_KEY_BACK);check(g_ctx_menu_open && !g_ctx_playlist_picker && !strcmp(lv_label_get_text(g_ctx_menu_items[g_ctx_menu_focus].label),"Add to Playlist"),"Picker Back returns to its parent action");
    check(songs_selected==selected_row,"Picker cancellation preserves underlying song selection");
    menu_choose("Add to Playlist");menu_choose("Mix A");check(!g_ctx_menu_open,"Selecting a destination closes the picker");
    check(player_playlist_get(1,&list) && list.track_count==2,"Adding a song appends once to the chosen list");
    check(player_playlist_track_get(1,1,&added) && added.id==selected.id,"Playlist append uses captured song identity");
    player_get_snapshot(&after);check(after.track.id==before.track.id && after.status==before.status && player_queue_count()==queue_before,"Playlist append leaves playback and queue unchanged");
    menu_open_key(CTX_TRACK,selected.id);menu_choose("Add to Playlist");player_next();pump(30);
    menu_choose("Mix B");check(player_playlist_track_get(2,0,&added) && added.id==selected.id,"Track changes do not redirect an open destination picker");
    menu_open_key(CTX_TRACK,selected.id);menu_choose("Add to Playlist");menu_choose("Back");
    check(g_ctx_menu_open && !g_ctx_playlist_picker,"Touch/Enter Back row returns to song actions");ui_handle_key(UI_KEY_BACK);
    // A deleted list cannot redirect to the item occupying its former index.
    menu_open_key(CTX_TRACK,selected.id);menu_choose("Add to Playlist");
    player_library_playlists_reset();player_library_playlist_item(0,"Replacement",0);pump(200);
    menu_choose("Mix A");check(player_playlist_get(1,&list) && list.track_count==0,"Frozen destination name is not replaced by an index after refresh");
    player_library_playlists_reset();pump(100);menu_open_key(CTX_TRACK,selected.id);menu_choose("Add to Playlist");
    check(menu_item_named("No playlists available")>=0 && g_ctx_playlist_count==0,"An empty destination list has a readable state and Back");
    menu_choose("Back");ui_handle_key(UI_KEY_BACK);
    // Every supported destination remains reachable, with a bounded scroll panel.
    for(int i=0;i<PLAYER_PLAYLIST_DESTINATIONS_MAX;i++){char name[32];snprintf(name,sizeof(name),"Destination %02d",i);player_library_playlist_item(i,name,0);}
    player_library_playlist_item(0,NULL,selected.id);pump(300);
    menu_open_key(CTX_TRACK,selected.id);menu_choose("Add to Playlist");
    check(g_ctx_playlist_count==PLAYER_PLAYLIST_DESTINATIONS_MAX && g_ctx_menu_item_count==PLAYER_PLAYLIST_DESTINATIONS_MAX+1,"Picker supports all 32 destinations plus Back");
    for(int i=0;i<PLAYER_PLAYLIST_DESTINATIONS_MAX;i++)ui_handle_key(UI_KEY_DOWN);
    check(g_ctx_menu_focus==g_ctx_menu_item_count-1,"Knob/key navigation reaches the last picker item");
    ui_handle_key(UI_KEY_CONTEXT);check(!g_ctx_menu_open && !g_ctx_playlist_destinations,"Closing the picker releases its name snapshot");
    // Playlist-detail songs expose the same context action through keys and touch.
    ui_select_screen(SCR_PLAYLISTS);pump(300);playlists_open((void*)(intptr_t)1);pump(300);
    int song_focus=-1;for(int i=0;i<g_focus_count;i++)if(g_focus[i].activate==playlists_track_action){song_focus=i;break;}
    check(song_focus>=0,"Playlist detail has a song");focus_select(song_focus);menu_open_key(CTX_TRACK,selected.id);
    menu_choose("Add to Playlist");menu_choose("Destination 01");
    check(player_playlist_track_get(2,0,&added) && added.id==selected.id,"Playlist song can be copied to another playlist");
    touch=lv_indev_create();lv_indev_set_type(touch,LV_INDEV_TYPE_POINTER);
    lv_indev_set_display(touch,lv_display_get_default());lv_indev_set_read_cb(touch,read_touch);ui_set_touch_enabled(true);ui_attach_touch(touch);
    player_get_snapshot(&before);hold_row_for_menu(g_focus[song_focus].obj,CTX_TRACK,selected.id);
    check(menu_item_named("Add to Playlist")>=0,"Playlist touch hold includes the destination action");
    ui_handle_key(UI_KEY_BACK);player_get_snapshot(&after);check(after.track.id==before.track.id,"Playlist long press does not play on release");
    ui_attach_touch(NULL);lv_indev_delete(touch);touch=NULL;
}

static void playlist_context_remove(void)
{
    player_track_t a,b,c,track;player_playlist_t list;player_snapshot_t before,after;
    check(player_song_get(0,&a)&&player_song_get(1,&b)&&player_song_get(2,&c),"Removal fixture has distinct songs");
    player_library_playlists_reset();player_library_playlist_item(0,"Remove mix",0);
    for(int i=0;i<4;i++)player_library_playlist_item(0,NULL,(uint64_t[]){a.id,b.id,a.id,c.id}[i]);
    player_library_playlist_item(1,"Other mix",0);player_library_playlist_item(1,NULL,c.id);pump(300);
    ui_select_screen(SCR_PLAYLISTS);pump(300);playlists_open((void*)(intptr_t)1);pump(300);
    int row=-1;for(int i=0;i<g_focus_count;i++)if(g_focus[i].activate==playlists_track_action&&(int)(intptr_t)g_focus[i].user==2){row=i;break;}
    check(row>=0,"Second duplicate is selectable");focus_select(row);menu_open_key(CTX_TRACK,a.id);
    check(menu_item_named("Remove from Playlist")>=0,"Regular playlist songs expose Remove from Playlist");
    context_menu_focus_set(menu_item_named("Remove from Playlist"));ui_navigation_t nav;ui_navigation_snapshot(&nav);
    check(!strcmp(nav.focus,"Remove from Playlist")&&(nav.flags&1),"Removal action is readable in ColoPlay navigation");
    player_get_snapshot(&before);int queue_count_before=player_queue_count();
    menu_choose("Remove from Playlist");check(!g_ctx_menu_open,"Removing closes the context menu");
    check(player_playlist_get(1,&list)&&list.track_count==3,"Removal deletes one occurrence");
    check(player_playlist_track_get(1,0,&track)&&track.id==a.id&&player_playlist_track_get(1,1,&track)&&track.id==b.id&&player_playlist_track_get(1,2,&track)&&track.id==c.id,"Deleting the second duplicate preserves its first occurrence and neighbors");
    check(player_playlist_get(2,&list)&&list.track_count==1,"Other playlists are unchanged");
    player_get_snapshot(&after);check(before.track.id==after.track.id&&before.status==after.status&&before.volume==after.volume&&player_queue_count()==queue_count_before,"Removal preserves playback, volume and queue");pump(300);
    // A changed source invalidates the captured position before dispatch.
    row=-1;for(int i=0;i<g_focus_count;i++)if(g_focus[i].activate==playlists_track_action){row=i;break;}
    check(row>=0,"Refreshed detail remains usable");focus_select(row);menu_open_key(CTX_TRACK,a.id);
    player_playlist_destination_t destination={0};snprintf(destination.name,sizeof(destination.name),"Remove mix");
    check(player_playlist_add(&destination,b.id),"Simulate playlist mutation while menu is open");
    menu_choose("Remove from Playlist");check(player_playlist_get(1,&list)&&list.track_count==4,"Stale menu does not delete after a playlist edit");pump(300);
    // Touch hold exposes the same action and does not play on release.
    row=-1;for(int i=0;i<g_focus_count;i++)if(g_focus[i].activate==playlists_track_action){row=i;break;}
    touch=lv_indev_create();lv_indev_set_type(touch,LV_INDEV_TYPE_POINTER);lv_indev_set_display(touch,lv_display_get_default());lv_indev_set_read_cb(touch,read_touch);ui_set_touch_enabled(true);ui_attach_touch(touch);
    hold_row_for_menu(g_focus[row].obj,CTX_TRACK,a.id);check(menu_item_named("Remove from Playlist")>=0,"Touch hold includes removal");
    ui_handle_key(UI_KEY_BACK);ui_attach_touch(NULL);lv_indev_delete(touch);touch=NULL;
    // Stared retains Unstar; the action does not appear outside playlist details.
    if(!player_track_starred(a.id))check(player_toggle_track_star(a.id),"Star fixture song");pump(300);
    playlists_open((void*)(intptr_t)0);pump(300);
    row=-1;for(int i=0;i<g_focus_count;i++)if(g_focus[i].activate==playlists_track_action){row=i;break;}
    check(row>=0,"Starred detail has a song");focus_select(row);ui_handle_key(UI_KEY_CONTEXT);
    check(g_ctx_menu_open&&menu_item_named("Unstar")>=0&&menu_item_named("Remove from Playlist")==-1,"Stared uses Unstar without a duplicate removal action");ui_handle_key(UI_KEY_BACK);
    ui_select_screen(SCR_SONGS);pump(300);songs_select(0);menu_open_key(CTX_TRACK,a.id);
    check(menu_item_named("Remove from Playlist")==-1,"Song library has no playlist-specific removal");ui_handle_key(UI_KEY_BACK);
    uint32_t rev=player_playlist_revision();check(!player_playlist_remove(0,0,a.id,rev)&&!player_playlist_remove(1,0,b.id,rev)&&!player_playlist_remove(1,0,a.id,rev-1),"Invalid source, key and revision do not remove anything");
    // Full server names survive UI events; deleting the sole entry leaves an empty playlist.
    char name[256];memset(name,'n',255);name[255]=0;
    player_library_playlists_reset();player_library_playlist_item(0,name,0);player_library_playlist_item(0,NULL,c.id);pump(300);
    player_playlist_destination_t full;check(player_playlist_destinations(&full,1)==1&&!strcmp(full.name,name),"Live model retains full playlist identity despite shortened UI labels");
    ui_select_screen(SCR_PLAYLISTS);pump(300);playlists_open((void*)(intptr_t)1);pump(300);
    row=-1;for(int i=0;i<g_focus_count;i++)if(g_focus[i].activate==playlists_track_action){row=i;break;}
    check(row>=0,"Final song is selectable");focus_select(row);menu_open_key(CTX_TRACK,c.id);menu_choose("Remove from Playlist");pump(300);
    check(player_playlist_get(1,&list)&&!list.track_count&&player_playlist_count()==2,"Empty playlist is retained after its final entry is removed");
    for(int i=0;i<g_focus_count;i++)check(g_focus[i].activate!=playlists_track_action,"Empty detail has no stale song controls");
}

int main(int argc,char **argv)
{
    lv_init();lv_display_t *display=lv_display_create(1280,720);
    lv_display_set_color_format(display,LV_COLOR_FORMAT_XRGB8888);
    lv_display_set_buffers(display,draw_buffer,NULL,sizeof(draw_buffer),LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(display,flush);
    ui_system_config_t config={.theme=argc>1?atoi(argv[1]):0,.brightness=45,.menubar_left=1,
        .fast_scroll_enabled=true,.fast_scroll_pages=2,.sort_desc=1,.song_sort_desc=1};
    ui_apply_saved_settings(&config);ui_init();pump(600);
    check(g_system_config.fast_scroll_hold_ms==300,"Missing hold configuration must default to 300 ms");
    check(g_system_config.fast_scroll_trigger_window_ms==550,"Missing trigger configuration must default to 550 ms");
    alpha_fade();now_playing_keys();library_context_menus();context_album_delayed_refresh();mini_unchanged_poll();artist_viewport();scrollbar_views();pressed_controls();fast_navigation();
    cursor_band_views();settings_widget_lifetime();rapid_tab_navigation();sort_order_settings();
    album_pane_retention();
    pagination_views();
    live_album_focus();
    live_song_focus();
    playlist_updates_retain_album_covers();
    volume_step_settings();
    remote_navigation_snapshot();
    playlist_context_picker();
    playlist_context_remove();
    albums_shuffle_all_action();
    puts("PASS: transparent text fade, Artists viewport restoration, column scrollbar geometry, page scrolling and theme cursor feedback");
    return 0;
}
