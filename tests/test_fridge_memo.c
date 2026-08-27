/* tests/test_fridge_memo.c — P0 host tests for api (pure) + page. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "fridge_memo_api.h"
#include "fridge_memo_dto.h"
#include "page_renderer.h"
#include "page_registry.h"
#include "data_refresh.h"

/* ---- helpers ---- */

static struct tm mk_today(int y, int m, int d)
{
    struct tm t;
    memset(&t, 0, sizeof(t));
    t.tm_year = y - 1900;
    t.tm_mon = m - 1;
    t.tm_mday = d;
    t.tm_hour = 12;
    mktime(&t);
    return t;
}

/* ---- api: parsing ---- */

static void test_parse_full_response(void)
{
    const char *json =
        "{\"updated_at\":\"2026-08-12T08:30:00+08:00\",\"items\":["
        "{\"id\":\"f_001\",\"name\":\"草莓\",\"quantity\":\"一盒\","
        "\"added_at\":\"2026-08-11\",\"expires_at\":\"2026-08-14\",\"note\":\"\",\"storage\":\"fridge\"},"
        "{\"id\":\"f_002\",\"name\":\"牛奶\",\"quantity\":\"半盒\","
        "\"added_at\":\"2026-08-10\",\"expires_at\":\"2026-08-13\"}]}";
    fridge_memo_snapshot_t snap;
    memset(&snap, 0, sizeof(snap));
    assert(fridge_memo_parse_items_json(json, &snap));
    assert(snap.count == 2);
    assert(strcmp(snap.items[0].id, "f_001") == 0);
    assert(strcmp(snap.items[0].name, "草莓") == 0);
    assert(strcmp(snap.items[1].expires_at, "2026-08-13") == 0);
    assert(snap.items[1].note[0] == '\0');
    assert(snap.items[1].storage[0] == '\0');
    /* UPDATED_LEN 24 truncates the 25-char ISO form to 23 chars + NUL */
    assert(strcmp(snap.updated_at, "2026-08-12T08:30:00+08:") == 0);
    printf("test_parse_full_response OK\n");
}

static void test_parse_bare_array(void)
{
    fridge_memo_snapshot_t snap;
    memset(&snap, 0, sizeof(snap));
    assert(fridge_memo_parse_items_json("[]", &snap));
    assert(snap.count == 0);
    printf("test_parse_bare_array OK\n");
}

static void test_parse_malformed_rejected(void)
{
    fridge_memo_snapshot_t snap;
    memset(&snap, 0, sizeof(snap));
    snap.count = 7; /* must stay untouched on failure */
    assert(!fridge_memo_parse_items_json("{not json", &snap));
    assert(snap.count == 7);
    assert(!fridge_memo_parse_items_json("{\"items\":\"nope\"}", &snap));
    printf("test_parse_malformed_rejected OK\n");
}

static void test_parse_truncates_at_64(void)
{
    char json[12288]; /* 100 items x ~89 bytes + wrapper ≈ 8.9 KB */
    size_t off = snprintf(json, sizeof(json), "{\"items\":[");
    for (int i = 0; i < 100; ++i) {
        off += snprintf(json + off, sizeof(json) - off,
                        "{\"id\":\"i%d\",\"name\":\"n\",\"quantity\":\"\",\"added_at\":\"2026-08-01\","
                        "\"expires_at\":\"2026-09-0%d\"},",
                        i, 1 + (i % 9));
    }
    off -= 1; /* trailing comma */
    snprintf(json + off, sizeof(json) - off, "]}");
    fridge_memo_snapshot_t snap;
    memset(&snap, 0, sizeof(snap));
    assert(fridge_memo_parse_items_json(json, &snap));
    struct tm today = mk_today(2026, 8, 12);
    fridge_memo_sort_snapshot(&snap, &today);
    assert(snap.count == FRIDGE_MEMO_MAX_ITEMS);
    printf("test_parse_truncates_at_64 OK\n");
}

static void test_parse_truncate_keeps_earliest(void)
{
    /* 200 items with strictly DESCENDING expiry (latest-expiry FIRST on the
     * wire) plus 50 no-expiry items at wire positions 130-180. The stage
     * holds the whole wire list, so the pre-truncate keeps the earliest-
     * expiry 64 — and with >64 expiry items present, no-expiry items never
     * survive. */
    char json[24576];
    char date[16];
    char earliest[16] = "";
    struct tm d = mk_today(2026, 8, 11);
    size_t off = snprintf(json, sizeof(json), "{\"items\":[");
    for (int i = 0; i < 200; ++i) {
        char added[16];
        snprintf(added, sizeof(added), "2026-06-%02d", 1 + (i % 28));
        bool no_expiry = i >= 130 && i < 180;
        if (!no_expiry) {
            d.tm_mday -= 1;
            mktime(&d); /* normalize across month boundaries */
            strftime(date, sizeof(date), "%Y-%m-%d", &d);
            if (i == 199)
                snprintf(earliest, sizeof(earliest), "%s", date);
        }
        off += snprintf(json + off, sizeof(json) - off,
                        "{\"id\":\"i%d\",\"name\":\"n\",\"quantity\":\"\",\"added_at\":\"%s\","
                        "\"expires_at\":\"%s\"},",
                        i, added, no_expiry ? "" : date);
    }
    off -= 1; /* trailing comma */
    snprintf(json + off, sizeof(json) - off, "]}");
    fridge_memo_snapshot_t snap;
    memset(&snap, 0, sizeof(snap));
    assert(fridge_memo_parse_items_json(json, &snap));
    struct tm today = mk_today(2026, 8, 12);
    fridge_memo_sort_snapshot(&snap, &today);
    assert(snap.count == FRIDGE_MEMO_MAX_ITEMS);
    assert(strcmp(snap.items[0].expires_at, earliest) == 0);
    /* zero no-expiry survive: 150 expiry items outrank the 64-cap */
    for (int i = 0; i < snap.count; ++i)
        assert(snap.items[i].expires_at[0] != '\0');
    printf("test_parse_truncate_keeps_earliest OK\n");
}

static void test_parse_truncate_no_expiry_tiebreak(void)
{
    /* 10 with-expiry + 60 no-expiry (70 total, cap 64): all expiry items
     * survive plus the 54 NEWEST-added no-expiry; the 6 oldest-added
     * no-expiry drop. No-expiry items are wired OLDEST-first so wire order
     * opposes the expected keep-set — only the added_at tiebreak can pass. */
    char json[8192];
    size_t off = snprintf(json, sizeof(json), "{\"items\":[");
    for (int i = 0; i < 10; ++i) {
        off += snprintf(json + off, sizeof(json) - off,
                        "{\"id\":\"e%d\",\"name\":\"exp\",\"quantity\":\"\",\"added_at\":\"2026-07-01\","
                        "\"expires_at\":\"2026-08-%02d\"},",
                        i, 20 - i);
    }
    struct tm base = mk_today(2026, 1, 1);
    for (int k = 0; k <= 59; ++k) { /* n0 (oldest-added) first on wire */
        char added[16];
        struct tm t = base;
        t.tm_mday += k; /* added_at strictly increasing in k */
        mktime(&t);
        strftime(added, sizeof(added), "%Y-%m-%d", &t);
        off += snprintf(json + off, sizeof(json) - off,
                        "{\"id\":\"n%d\",\"name\":\"unk\",\"quantity\":\"\",\"added_at\":\"%s\","
                        "\"expires_at\":\"\"},",
                        k, added);
    }
    off -= 1; /* trailing comma */
    snprintf(json + off, sizeof(json) - off, "]}");
    fridge_memo_snapshot_t snap;
    memset(&snap, 0, sizeof(snap));
    assert(fridge_memo_parse_items_json(json, &snap));
    assert(snap.count == FRIDGE_MEMO_MAX_ITEMS);
    /* first 10: the with-expiry items */
    for (int i = 0; i < 10; ++i)
        assert(snap.items[i].expires_at[0] != '\0');
    /* then the 54 newest-added no-expiry, newest first: n59..n6 */
    for (int k = 59; k >= 6; --k) {
        char id[8];
        snprintf(id, sizeof(id), "n%d", k);
        assert(strcmp(snap.items[10 + (59 - k)].id, id) == 0);
    }
    /* the 6 oldest-added no-expiry are dropped */
    for (int k = 0; k < 6; ++k) {
        char id[8];
        snprintf(id, sizeof(id), "n%d", k);
        bool found = false;
        for (int i = 0; i < snap.count; ++i)
            if (strcmp(snap.items[i].id, id) == 0)
                found = true;
        assert(!found);
    }
    printf("test_parse_truncate_no_expiry_tiebreak OK\n");
}

/* ---- api: date math ---- */

static void test_days_math(void)
{
    struct tm today = mk_today(2026, 8, 12);
    assert(fridge_memo_days_since("2026-08-12", &today) == 1); /* same day = day 1 */
    assert(fridge_memo_days_since("2026-08-11", &today) == 2);
    assert(fridge_memo_days_since("2026-07-28", &today) == 16); /* cross-month */
    assert(fridge_memo_days_until("2026-08-12", &today) == 0);
    assert(fridge_memo_days_until("2026-08-14", &today) == 2);
    assert(fridge_memo_days_until("2026-08-09", &today) == -3);
    assert(fridge_memo_days_until("", &today) == -1000);
    assert(fridge_memo_days_since("bad", &today) == -1);
    printf("test_days_math OK\n");
}

/* ---- api: status + sort ---- */

static void test_derive_status(void)
{
    struct tm today = mk_today(2026, 8, 12);
    fridge_memo_item_t it;
    memset(&it, 0, sizeof(it));
    strcpy(it.added_at, "2026-08-01");
    strcpy(it.expires_at, "2026-08-09");
    assert(fridge_memo_derive_status(&it, &today) == FRIDGE_MEMO_STATUS_EXPIRED);
    strcpy(it.expires_at, "2026-08-12");
    assert(fridge_memo_derive_status(&it, &today) == FRIDGE_MEMO_STATUS_NEAR);
    strcpy(it.expires_at, "2026-08-14");
    assert(fridge_memo_derive_status(&it, &today) == FRIDGE_MEMO_STATUS_NEAR);
    strcpy(it.expires_at, "2026-08-15");
    assert(fridge_memo_derive_status(&it, &today) == FRIDGE_MEMO_STATUS_OK);
    it.expires_at[0] = '\0';
    assert(fridge_memo_derive_status(&it, &today) == FRIDGE_MEMO_STATUS_UNKNOWN);
    strcpy(it.expires_at, "2026-08-20");
    assert(fridge_memo_derive_status(&it, NULL) == FRIDGE_MEMO_STATUS_UNKNOWN); /* degraded */
    printf("test_derive_status OK\n");
}

static void test_sort_order(void)
{
    fridge_memo_snapshot_t snap;
    memset(&snap, 0, sizeof(snap));
    /* index: 0=ok(5d) 1=expired(-2d) 2=near(1d) 3=unknown 4=expired(-5d) 5=near(0d) */
    const char *names[] = {"a_ok", "b_exp2", "c_near1", "d_unk", "e_exp5", "f_near0"};
    const char *exp[] = {"2026-08-17", "2026-08-10", "2026-08-13", "", "2026-08-07", "2026-08-12"};
    const char *added[] = {"2026-08-01", "2026-08-01", "2026-08-02", "2026-08-05", "2026-08-01", "2026-08-03"};
    snap.count = 6;
    for (int i = 0; i < 6; ++i) {
        strcpy(snap.items[i].name, names[i]);
        strcpy(snap.items[i].expires_at, exp[i]);
        strcpy(snap.items[i].added_at, added[i]);
    }
    struct tm today = mk_today(2026, 8, 12);
    fridge_memo_sort_snapshot(&snap, &today);
    /* expected: e_exp5, b_exp2, f_near0, c_near1, a_ok, d_unk */
    assert(strcmp(snap.items[0].name, "e_exp5") == 0);
    assert(strcmp(snap.items[1].name, "b_exp2") == 0);
    assert(strcmp(snap.items[2].name, "f_near0") == 0);
    assert(strcmp(snap.items[3].name, "c_near1") == 0);
    assert(strcmp(snap.items[4].name, "a_ok") == 0);
    assert(strcmp(snap.items[5].name, "d_unk") == 0);
    assert(fridge_memo_count_by_status(&snap, FRIDGE_MEMO_STATUS_EXPIRED, &today) == 2);
    assert(fridge_memo_count_by_status(&snap, FRIDGE_MEMO_STATUS_NEAR, &today) == 2);
    printf("test_sort_order OK\n");
}

static void test_sort_degraded(void)
{
    fridge_memo_snapshot_t snap;
    memset(&snap, 0, sizeof(snap));
    snap.count = 3;
    strcpy(snap.items[0].name, "old");
    strcpy(snap.items[0].added_at, "2026-07-01");
    strcpy(snap.items[1].name, "newest");
    strcpy(snap.items[1].added_at, "2026-08-10");
    strcpy(snap.items[2].name, "mid");
    strcpy(snap.items[2].added_at, "2026-08-01");
    fridge_memo_sort_snapshot(&snap, NULL);
    assert(strcmp(snap.items[0].name, "newest") == 0);
    assert(strcmp(snap.items[1].name, "mid") == 0);
    assert(strcmp(snap.items[2].name, "old") == 0);
    printf("test_sort_degraded OK\n");
}

/* ---- api: config roundtrip (host path: params only, no NVS) ---- */

static void test_base_url_roundtrip(void)
{
    fridge_memo_api_set_base_url("http://192.168.1.10:8000");
    char buf[96];
    fridge_memo_api_get_base_url(buf, sizeof(buf));
    assert(strcmp(buf, "http://192.168.1.10:8000") == 0);
    fridge_memo_api_set_base_url(NULL);
    fridge_memo_api_get_base_url(buf, sizeof(buf));
    assert(buf[0] == '\0');
    fridge_memo_api_set_base_url("");
    fridge_memo_api_get_base_url(buf, sizeof(buf));
    assert(buf[0] == '\0'); /* empty string clears */
    printf("test_base_url_roundtrip OK\n");
}
/* ---- page tests ---- */

#include "fridge_memo_page.h"
#include "rawdraw_ext.h"
#include "theme.h"
#include <stdlib.h>

/* Functional font mocks for the font_engine.h declarations the page binds
 * (8px advance, solid 6x6 glyph — same shape as test_ui_pages_smoke.c, but
 * functional so render-smoke can assert real ink). */
static bool fm_mock_get_glyph_dsc(const struct _lv_font_t *font, lv_font_glyph_dsc_t *dsc, uint32_t letter,
                                  uint32_t letter_next)
{
    (void)font;
    (void)letter;
    (void)letter_next;
    dsc->resolved_font = font;
    dsc->adv_w = 8;
    dsc->box_w = 6;
    dsc->box_h = 6;
    dsc->ofs_x = 1;
    dsc->ofs_y = 1;
    dsc->stride = 1;
    dsc->format = LV_FONT_GLYPH_FORMAT_A1;
    return true;
}
static const uint8_t fm_mock_bitmap[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
static const void *fm_mock_get_glyph_bitmap(lv_font_glyph_dsc_t *g, struct _lv_draw_buf_t *db)
{
    (void)g;
    (void)db;
    return fm_mock_bitmap;
}
const lv_font_t SourceHanSansSC_Regular_slim = {
    .get_glyph_dsc = fm_mock_get_glyph_dsc,
    .get_glyph_bitmap = fm_mock_get_glyph_bitmap,
    .release_glyph = NULL,
    .line_height = 16,
    .base_line = 3,
};
const lv_font_t SourceHanSansSC_Medium_slim = {
    .get_glyph_dsc = fm_mock_get_glyph_dsc,
    .get_glyph_bitmap = fm_mock_get_glyph_bitmap,
    .release_glyph = NULL,
    .line_height = 24,
    .base_line = 5,
};
const lv_font_t font_zectrix_16_1 = {
    .get_glyph_dsc = fm_mock_get_glyph_dsc,
    .get_glyph_bitmap = fm_mock_get_glyph_bitmap,
    .release_glyph = NULL,
    .line_height = 16,
    .base_line = 3,
};

/* Cast helper: base is the first member, zero-cost downcast. */
#define g_page_of(r) ((fridge_memo_page_t *)(r))

static void fill_snapshot_9(fridge_memo_snapshot_t *snap)
{
    memset(snap, 0, sizeof(*snap));
    snap->count = 9;
    /* pre-sorted urgency order: 2 expired, 1 near, 6 ok */
    strcpy(snap->items[0].name, "酸奶");
    strcpy(snap->items[0].id, "e1");
    strcpy(snap->items[0].quantity, "5连杯");
    strcpy(snap->items[0].added_at, "2026-07-28");
    strcpy(snap->items[0].expires_at, "2026-08-09");
    strcpy(snap->items[1].name, "剩菜");
    strcpy(snap->items[1].id, "e2");
    strcpy(snap->items[1].added_at, "2026-08-05");
    strcpy(snap->items[1].expires_at, "2026-08-10");
    strcpy(snap->items[2].name, "牛奶");
    strcpy(snap->items[2].id, "n1");
    strcpy(snap->items[2].quantity, "半盒");
    strcpy(snap->items[2].added_at, "2026-08-10");
    strcpy(snap->items[2].expires_at, "2026-08-13");
    for (int i = 3; i < 9; ++i) {
        strcpy(snap->items[i].name, "菜");
        snap->items[i].name[1] = (char)('0' + i);
        strcpy(snap->items[i].id, "ok");
        strcpy(snap->items[i].added_at, "2026-08-01");
        strcpy(snap->items[i].expires_at, "2026-08-25");
    }
}

static void test_page_paging_math(void)
{
    fridge_memo_page_t stack_page; /* use ops directly, not the singleton */
    memset(&stack_page, 0, sizeof(stack_page));
    fridge_memo_snapshot_t snap;
    fill_snapshot_9(&snap);
    fridge_memo_page_update((page_renderer_t *)&stack_page, &snap);
    assert(fridge_memo_page_count(&stack_page) == 9);
    assert(fridge_memo_page_pages(&stack_page) == 3); /* ceil(9/4) */
    assert(fridge_memo_page_rows_on_page(&stack_page, 0) == 4);
    assert(fridge_memo_page_rows_on_page(&stack_page, 2) == 1);
    assert(fridge_memo_page_rows_on_page(&stack_page, 5) == 0);
    printf("test_page_paging_math OK\n");
}

static void test_page_flip_clamps(void)
{
    fridge_memo_page_t p;
    memset(&p, 0, sizeof(p));
    fridge_memo_snapshot_t snap;
    fill_snapshot_9(&snap);
    fridge_memo_page_update((page_renderer_t *)&p, &snap);
    ui_button_event_t dn = {BTN_DOWN_CLICK};
    ui_button_event_t up = {BTN_UP_CLICK};
    assert(fridge_memo_page_handle_input((page_renderer_t *)&p, &dn)); /* 0->1 */
    assert(p.page_index == 1);
    assert(fridge_memo_page_handle_input((page_renderer_t *)&p, &dn)); /* 1->2 */
    assert(fridge_memo_page_handle_input((page_renderer_t *)&p, &dn) == false); /* clamp */
    assert(p.page_index == 2);
    assert(fridge_memo_page_handle_input((page_renderer_t *)&p, &up)); /* 2->1 */
    assert(fridge_memo_page_handle_input((page_renderer_t *)&p, &up));
    assert(fridge_memo_page_handle_input((page_renderer_t *)&p, &up) == false); /* clamp */
    assert(p.page_index == 0);
    printf("test_page_flip_clamps OK\n");
}

static ui_button_event_t *up_ev(void)
{
    static ui_button_event_t e = {BTN_UP_CLICK};
    return &e;
}

static char g_deleted_id[16];
static int g_delete_calls;
static void fake_delete(const char *id, void *ctx)
{
    (void)ctx;
    snprintf(g_deleted_id, sizeof(g_deleted_id), "%s", id);
    ++g_delete_calls;
}

static void test_delete_overlay_flow(void)
{
    fridge_memo_page_t p;
    memset(&p, 0, sizeof(p));
    fridge_memo_snapshot_t snap;
    fill_snapshot_9(&snap);
    fridge_memo_page_update((page_renderer_t *)&p, &snap);
    fridge_memo_page_set_delete_request_handler((page_renderer_t *)&p, fake_delete, NULL);
    ui_button_event_t dbl = {BTN_BOOT_DOUBLE_CLICK};
    ui_button_event_t dn = {BTN_DOWN_CLICK};
    ui_button_event_t boot = {BTN_BOOT_CLICK};
    /* open on page 0: focus defaults to row 0 (e1) */
    assert(fridge_memo_page_handle_input((page_renderer_t *)&p, &dbl));
    assert(p.showing_delete && p.delete_focus == 0);
    /* focus moves within the 4 rows of THIS page; UP=up/DOWN=down per the
     * repo-wide list convention (news/settings), wrapping at the edges */
    assert(fridge_memo_page_handle_input((page_renderer_t *)&p, &dn)); /* 0->1 */
    assert(p.delete_focus == 1);
    assert(fridge_memo_page_handle_input((page_renderer_t *)&p, up_ev())); /* 1->0 */
    assert(p.delete_focus == 0);
    assert(fridge_memo_page_handle_input((page_renderer_t *)&p, up_ev())); /* 0->3 wrap */
    assert(p.delete_focus == 3);
    assert(fridge_memo_page_handle_input((page_renderer_t *)&p, &dn)); /* 3->0 wrap */
    assert(p.delete_focus == 0);
    /* BOOT confirms the focused item */
    assert(fridge_memo_page_handle_input((page_renderer_t *)&p, &boot));
    assert(g_delete_calls == 1 && strcmp(g_deleted_id, "e1") == 0);
    assert(!p.showing_delete);
    /* double-click toggles open->close */
    assert(fridge_memo_page_handle_input((page_renderer_t *)&p, &dbl));
    assert(p.showing_delete);
    assert(fridge_memo_page_handle_input((page_renderer_t *)&p, &dbl));
    assert(!p.showing_delete);
    /* update() resets view state (design §4.3 r8) */
    p.page_index = 2;
    p.showing_delete = true;
    fridge_memo_page_update((page_renderer_t *)&p, &snap);
    assert(p.page_index == 0);
    assert(!p.showing_delete);
    printf("test_delete_overlay_flow OK\n");
}

static void test_render_smoke(void)
{
    /* singleton via registry (constructor ran at load) */
    page_renderer_t *r = page_registry_get_instance(UI_PAGE_FRIDGE_MEMO);
    assert(r != NULL);
    assert(strcmp(page_registry_get_name(UI_PAGE_FRIDGE_MEMO), "冰箱备忘") == 0);
    int fb_bytes = (400 * 300) / 4; /* 2bpp */
    uint8_t *fb = calloc(1, fb_bytes);
    assert(fb);
    fridge_memo_snapshot_t snap;
    fill_snapshot_9(&snap);
    fridge_memo_page_init(r, 400, 300); /* init first: init wipes state */
    fridge_memo_page_update(r, &snap);
    fridge_memo_page_render(r, fb, 400, 300);
    int ink = 0;
    for (int i = 0; i < fb_bytes; ++i)
        if (fb[i] != 0x55) /* 0x55 = all-white 2bpp pattern per rawdraw_ext.c */
            ++ink;
    assert(ink > 1000); /* not blank */
    /* empty state */
    fridge_memo_snapshot_t empty;
    memset(&empty, 0, sizeof(empty));
    fridge_memo_page_update(r, &empty);
    memset(fb, 0x55, fb_bytes);
    fridge_memo_page_render(r, fb, 400, 300);
    int empty_ink = 0;
    for (int i = 0; i < fb_bytes; ++i)
        if (fb[i] != 0x55)
            ++empty_ink;
    assert(empty_ink > 0); /* regression: blank empty screen */
    /* delete overlay renders */
    g_page_of(r)->showing_delete = true;
    fridge_memo_page_render(r, fb, 400, 300);
    free(fb);
    printf("test_render_smoke OK\n");
}

int main(void)
{
    test_parse_full_response();
    test_parse_bare_array();
    test_parse_malformed_rejected();
    test_parse_truncates_at_64();
    test_parse_truncate_keeps_earliest();
    test_parse_truncate_no_expiry_tiebreak();
    test_days_math();
    test_derive_status();
    test_sort_order();
    test_sort_degraded();
    test_base_url_roundtrip();
    test_page_paging_math();
    test_page_flip_clamps();
    test_delete_overlay_flow();
    test_render_smoke();
    printf("ALL FRIDGE MEMO TESTS PASSED\n");
    return 0;
}
