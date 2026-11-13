/* Real LVGL pages, populated through the MPD metadata/catalog interface. */
#include "../app/ui/ui.c"
static uint8_t draw_buffer[1280*40*4];
static void check(bool ok,const char *message)
{ if(!ok) { fprintf(stderr,"FAIL: %s\n",message); exit(1); } }
static void flush(lv_display_t *d,const lv_area_t *area,uint8_t *pixels)
{ LV_UNUSED(area); LV_UNUSED(pixels); lv_display_flush_ready(d); }
static void pump(int ms)
{ for(int i=0;i<ms;i+=10) { lv_tick_inc(10);lv_timer_handler(); } }
static void add(unsigned id,unsigned album,const char *name,const char *artist,
                const char *title,unsigned date,unsigned year,unsigned track)
{
    player_media_item_t p={.pid=id,.album_pid=album,.date_added=date,.year=year,
        .duration_ms=240000,.track_no=track,.fields=PLAYER_MEDIA_MPD|(1u<<1)|(1u<<4)|
        (1u<<5)|(1u<<6)|(1u<<7)|(1u<<9)|(1u<<12)|(1u<<14)};
    snprintf(p.title,sizeof(p.title),"%s",title);snprintf(p.album,sizeof(p.album),"%s",name);
    snprintf(p.artist,sizeof(p.artist),"%s",artist);snprintf(p.album_artist,sizeof(p.album_artist),"%s",artist);
    player_library_update(&p);
}
static uint64_t queue_id(int i)
{ player_track_t t;return player_queue_at(i,&t)?t.id:0; }
static void queue_is(const uint64_t *ids,int count,const char *message)
{
    check(player_queue_count()==count,message);
    for(int i=0;i<count;i++)check(queue_id(i)==ids[i],message);
}
static void fixture(void)
{
    player_library_reset();
    add(90001,80001,"Beta","Zulu","Second",2000,2022,2);
    add(90002,80002,"Alpha","Alpha","First",1000,2020,1);
    add(90003,80001,"Beta","Zulu","Third",3000,2022,3);
    add(90004,80003,"Gamma","Zulu","Fourth",4000,2024,1);
    add(90005,80001,"Beta","Zulu","Fifth",2000,2022,1);
    player_library_complete();
    player_library_playlists_reset();player_library_playlist_item(0,"Mix",0);
    const uint64_t playlist[]={90004,90002,90004,90001};
    for(unsigned i=0;i<4;i++)player_library_playlist_item(0,NULL,playlist[i]);
    player_play_song(90001);pump(300);
}
static void order(int field,bool desc)
{
    ui_system_config_t cfg=g_system_config;
    cfg.sort_field=field;cfg.sort_desc=desc;
    cfg.song_sort_field=field;cfg.song_sort_desc=desc;
    ui_set_system_config(&cfg);pump(400);
}
static void albums(void)
{
    ui_select_screen(SCR_ALBUMS);pump(600);
    for(int field=0;field<3;field++)for(int desc=0;desc<2;desc++) {
        order(field,desc);
        player_album_t a;check(player_album_get(0,&a),"Album fixture exists");
        check(a.id==(desc?80003:80002),"Album date/name/year order must reach the displayed list");
        check(!strcmp(lv_label_get_text(albums_row_titles[0]),a.title),"Rendered first album must match backend order");
    }
    int beta=-1;
    for(int i=0;i<player_album_count();i++) { player_album_t a;player_album_get(i,&a);if(a.id==80001)beta=i; }
    albums_select(beta);pump(200);albums_open((void*)(intptr_t)beta);pump(200);
    albums_play_all_action(NULL);
    queue_is((uint64_t[]){90005,90001,90003},3,"Album Play must follow track numbers without disc tags");
    albums_track_action((void*)1);
    queue_is((uint64_t[]){90001,90003},2,"Album track row must play from that row");
    check(albums_back(),"Back must exit album detail");
}
static void artists(void)
{
    ui_select_screen(SCR_ARTISTS);pump(600);
    check(player_artist_count()==2,"Empty artists from the old library must disappear");
    player_artist_t a;player_artist_get(0,&a);check(!strcmp(a.name,"Alpha"),"Artist master list must be alphabetic");
    artists_select(1);pump(200);artists_open((void*)1);pump(200);
    artists_play_all_action(NULL);
    queue_is((uint64_t[]){90005,90001,90003,90004},4,"Artist Play must match album groups and track order");
    artists_track_action((void*)2);
    queue_is((uint64_t[]){90003,90004,90005,90001},4,"Artist row must start that visible track and wrap its artist");
    check(artists_back(),"Back must exit artist detail");
}
static void songs(void)
{
    ui_select_screen(SCR_SONGS);pump(600);
    for(int field=0;field<3;field++)for(int desc=0;desc<2;desc++) {
        order(field,desc);songs_select(2);pump(100);
        uint64_t expected[5];
        for(int i=0;i<5;i++){player_track_t t;player_song_get((i+2)%5,&t);expected[i]=t.id;}
        songs_activate();queue_is(expected,5,"Songs row must use the currently displayed sort order");
    }
}
static void playlists(void)
{
    ui_select_screen(SCR_PLAYLISTS);pump(600);playlists_selected=1;
    playlists_open((void*)1);pump(200);playlists_play_action(NULL);
    queue_is((uint64_t[]){90004,90002,90004,90001},4,"Playlist Play must retain duplicates and order");
    playlists_track_action((void*)2);
    queue_is((uint64_t[]){90004,90001},2,"Playlist track row must start from its occurrence");
    check(playlists_back(),"Back must exit playlist detail");
}
static void now_playing(void)
{
    ui_select_screen(SCR_NOW_PLAYING);pump(600);player_play_album(80001);pump(100);
    player_snapshot_t s;player_get_snapshot(&s);check(s.track.id==90005,"Now Playing must show selected track");
    np_play_click_cb(NULL);player_get_snapshot(&s);check(s.status==PLAYER_PAUSED,"Play/Pause must pause");
    np_play_click_cb(NULL);player_get_snapshot(&s);check(s.status==PLAYER_PLAYING,"Play/Pause must resume");
    np_next_click_cb(NULL);player_get_snapshot(&s);check(s.track.id==90001,"Next must select next queued track");
    player_seek_sec(0);np_prev_click_cb(NULL);player_get_snapshot(&s);check(s.track.id==90005,"Previous must select preceding track");
    np_star_click_cb(NULL);player_get_snapshot(&s);check(s.starred,"Star control must update current metadata");
    check(player_playlist_track_count(0)==1,"Star must populate local Starred view");
    np_repeat_click_cb(NULL);np_shuffle_click_cb(NULL);np_tick();pump(100);
    np_toggle_queue();pump(300);check(np_queue_list!=NULL,"Up Next must build the queue view");
    check(player_queue_count()==3,"Up Next must retain current queue");
    player_queue_jump(2);np_tick();player_get_snapshot(&s);check(s.track.id==90003,"Up Next jump must select requested occurrence");
    np_toggle_queue();pump(100);
    /* Context destinations must also become visible when far down a list. */
    for(int i=0;i<20;i++) {
        char artist_name[40];snprintf(artist_name,sizeof(artist_name),"Artist %02d",i);
        add(91000+i,81000+i,"Other album",artist_name,"Other song",5000,2025,1);
    }
    player_library_complete();pump(200);
    context_menu_go_album(NULL);pump(600);
    check(g_current_screen==SCR_ALBUMS && albums_detail_open,"Go to Album must open the album detail");
    player_album_t album;player_album_get(albums_selected,&album);
    check(album.id==80001,"Go to Album must select the playing album");
    check(lv_obj_get_scroll_y(albums_left_list)>0,"Go to Album must reveal an offscreen destination");
    ui_select_screen(SCR_NOW_PLAYING);pump(300);
    context_menu_go_artist(NULL);pump(600);
    check(g_current_screen==SCR_ARTISTS && artists_detail_open,"Go to Artist must open the artist detail");
    player_artist_t artist;player_artist_get(artists_selected,&artist);
    check(!strcmp(artist.name,"Zulu"),"Go to Artist must select the playing track's album artist");
    check(lv_obj_get_scroll_y(artists_left_list)>0,"Go to Artist must reveal an offscreen destination");
}
static void settings(void)
{
    ui_select_screen(SCR_SETTINGS);pump(600);
    for(int i=0;i<SETTING_COUNT;i++) {
        settings_open((void*)(intptr_t)i);pump(250);
        check(settings_right_content && lv_obj_get_child_count(settings_right_content)>0,"Every settings pane must render");
    }
    settings_open((void*)SETTING_AFC_IAP2);pump(200);
    check(!strcmp(settings_names[SETTING_AFC_IAP2],"MPD Server"),"First settings pane must describe MPD");
    check(settings_mpd_cache_count,"MPD Server must show the live artwork cache count");
    check(lv_obj_get_child_count(settings_right_content)==5,
          "MPD settings must contain only server, connection, albums, tracks and artwork cache");
    lv_obj_t *cache_row=lv_obj_get_parent(settings_mpd_cache_count);
    check(!strcmp(lv_label_get_text(lv_obj_get_child(cache_row,0)),"Artwork Cache"),
          "The cache row must use the requested label");
    check(lv_obj_get_style_text_font(settings_mpd_cache_count,0)==FONT_H2 &&
          lv_obj_get_height(cache_row)==SETTINGS_ROW_H,
          "Artwork Cache must use the same font and row height as other info rows");
    settings_mpd_cache_values(8,10);
    check(!strcmp(lv_label_get_text(settings_mpd_cache_count),"8 / 10 (80%)"),
          "Cache percentage must describe cached albums rather than checked missing images");
    settings_mpd_cache_values(0,0);
    check(!strcmp(lv_label_get_text(settings_mpd_cache_count),"0 / 0 (0%)"),
          "Empty cache progress must remain well defined");
    settings_open((void*)SETTING_ABOUT);pump(200);
    check(!settings_mpd_cache_count,"Leaving MPD settings must clear the deleted count reference");
    ui_select_screen(SCR_BLUETOOTH);pump(400);check(g_focus_count>0,"Bluetooth view must retain accessible controls");
    ui_select_screen(SCR_NOW_PLAYING);pump(400);
    for(int i=0;i<5;i++){ui_handle_key(UI_KEY_SCREEN_NEXT);pump(300);}
    check(g_current_screen==SCR_SETTINGS,"Tab navigation must reach Settings while skipping the Bluetooth subpage");
}
static void connection_placeholder(void)
{
    lv_obj_t *parent=lv_obj_create(lv_screen_active());
    np_placeholder(parent);
    lv_obj_t *box=lv_obj_get_child(parent,0);
    check(box && lv_obj_get_child_count(box)==1,
          "Waiting for MPD must show only a single connection message");
    lv_obj_t *label=lv_obj_get_child(box,0);
    check(!strcmp(lv_label_get_text(label),"Connecting to MPD server"),
          "An unconfigured connection must use the simple waiting message");
    check(lv_obj_get_style_text_font(label,0)==FONT_H2 &&
          lv_color_eq(lv_obj_get_style_text_color(label,0),lv_color_hex(COL_MUTED)),
          "The connecting message must retain the previous connection-status font and color");
    lv_obj_delete(parent);
}
int main(int argc,char **argv)
{
    lv_init();lv_display_t *d=lv_display_create(1280,720);
    lv_display_set_color_format(d,LV_COLOR_FORMAT_XRGB8888);
    lv_display_set_buffers(d,draw_buffer,NULL,sizeof(draw_buffer),LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(d,flush);
    ui_system_config_t cfg={.theme=argc>1?atoi(argv[1]):0,.brightness=45,.menubar_left=1,.sort_desc=1,.song_sort_desc=1,.fast_scroll_pages=2};
    ui_apply_saved_settings(&cfg);ui_init();pump(600);fixture();
    albums();artists();songs();playlists();now_playing();settings();connection_placeholder();
    puts("All MPD view actions passed");return 0;
}
