/* Production LVGL Up Next against a full-size queue, with controllable page arrival. */
#include "../app/ui/ui.c"
static uint8_t draw_buffer[1280*40*4];
static bool deferred;
static int reads;
bool __real_player_queue_at(int index,player_track_t *out);
bool __wrap_player_queue_at(int index,player_track_t *out)
{reads++;return !deferred&&__real_player_queue_at(index,out);}
bool __wrap_player_queue_loading(int index)
{return deferred&&index>=0&&index<player_queue_count();}
static void check(bool ok,const char *message)
{if(!ok){fprintf(stderr,"FAIL: %s\n",message);exit(1);}}
static void flush(lv_display_t *display,const lv_area_t *area,uint8_t *pixels)
{LV_UNUSED(area);LV_UNUSED(pixels);lv_display_flush_ready(display);}
static void pump(int ms)
{for(int i=0;i<ms;i+=10){lv_tick_inc(10);lv_timer_handler();}}
static void pool_unchanged(lv_obj_t **pool)
{
    check(lv_obj_get_child_count(np_queue_list)==NP_QUEUE_POOL+2,"Full queue owns only the fixed pool and two extent/status objects");
    for(int i=0;i<NP_QUEUE_POOL;i++)check(np_queue_rows[i]==pool[i],"Scrolling reuses row widgets");
    check(g_focus_count==np_queue_focus_start+NP_QUEUE_POOL,"Focus storage stays bounded by the row pool");
}
int main(int argc,char **argv)
{
    lv_init();lv_display_t *display=lv_display_create(1280,720);
    lv_display_set_color_format(display,LV_COLOR_FORMAT_XRGB8888);
    lv_display_set_buffers(display,draw_buffer,NULL,sizeof(draw_buffer),LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(display,flush);
    ui_system_config_t cfg={.theme=argc>1?atoi(argv[1]):0,.brightness=45,.fast_scroll_enabled=false};
    ui_apply_saved_settings(&cfg);ui_init();pump(100);
    player_track_t track;check(player_song_get(0,&track),"Fixture has a song");player_play_song(track.id);
    while(player_queue_count()<20916)check(player_queue_add(PLAYER_SELECTION_TRACK,track.id,false),"Large queue appends duplicate occurrences");
    player_snapshot_t before;player_get_snapshot(&before);if(before.status==PLAYER_PLAYING)player_play_pause();
    player_get_snapshot(&before);ui_select_screen(SCR_NOW_PLAYING);pump(200);
    deferred=true;np_toggle_queue();pump(200);
    lv_obj_t *pool[NP_QUEUE_POOL];for(int i=0;i<NP_QUEUE_POOL;i++)pool[i]=np_queue_rows[i];
    int base=np_queue_base,total=np_queue_total;
    check(total==20916&&total-base>100,"Up Next represents the entire remaining queue");
    check(lv_obj_get_height(np_queue_spacer)==(total-base)*NP_QUEUE_ROW_H,"Scrollbar reserves the full logical extent");
    ui_handle_key_repeat(UI_KEY_DOWN,260);pump(100);
    check(np_queue_selected==base+260,"Knob bursts cross the old 100-row limit while metadata is pending");
    context_target_t context;check(!context_capture(&context),"Loading rows cannot target a stale queue occurrence");
    ui_handle_key(UI_KEY_ENTER);player_snapshot_t after;player_get_snapshot(&after);
    check(after.track.id==before.track.id&&player_queue_index()==base-1,"Loading-row Enter does not jump playback");
    pool_unchanged(pool);
    ui_handle_key_repeat(UI_KEY_DOWN,30000);pump(100);
    check(np_queue_selected==total-1,"Knob can reach the final entry of a 20,916-item queue");
    ui_navigation_t nav;ui_navigation_snapshot(&nav);
    check(nav.count==(uint32_t)(total-base)&&nav.index==nav.count-1&&!strcmp(nav.focus,"Loading..."),"Navigation reports the full count and logical position during loading");
    int prior_reads=reads;deferred=false;pump(200);
    check(reads-prior_reads<=NP_QUEUE_POOL+1,"Page arrival only binds the visible pool");
    check(context_capture(&context)&&context.entry==player_queue_entry(total-1),"Recycled row targets the exact final occurrence, including duplicate songs");
    int end_slot=g_focus_index-np_queue_focus_start;
    check(end_slot>=0&&np_queue_row_keys[end_slot]&&np_queue_row_indices[end_slot]==total-1,"Last selected row receives its own metadata");
    ui_navigation_snapshot(&nav);check(nav.index==nav.count-1&&nav.count>20000&&strcmp(nav.focus,"Loading..."),"Confirmed metadata preserves logical navigation");
    pool_unchanged(pool);
    ui_handle_key(UI_KEY_PAGE_UP);check(np_queue_selected<total-1&&np_queue_selected>100,"Page Up moves within the full queue");
    int saved=np_queue_selected;ui_handle_key(UI_KEY_CONTEXT);check(g_ctx_menu_open,"Far queue entry exposes its context menu");
    focus_item_t menu_focus=g_focus[g_focus_index];np_queue_fill_cb(np_queue_fill_timer);
    check(g_focus[g_focus_index].obj==menu_focus.obj&&g_focus[g_focus_index].user==menu_focus.user,"Pending page timer does not overwrite modal focus");
    ui_handle_key(UI_KEY_BACK);check(np_queue_selected==saved,"Closing context menu preserves queue selection");
    np_queue_select(total-1);ui_handle_key(UI_KEY_ENTER);pump(200);
    check(player_queue_index()==total-1,"Enter jumps to the exact selected occurrence at the end");
    check(np_queue_total==np_queue_base,"End-of-queue view is empty without stale selectable rows");
    ui_handle_key(UI_KEY_BACK);check(!np_queue_visible,"Back closes the virtual queue");
    np_queue_fill_cb(np_queue_fill_timer);
    puts("PASS: full queue, bounded recycled widgets, pending pages, far navigation, context identity and final jump");
    return 0;
}
