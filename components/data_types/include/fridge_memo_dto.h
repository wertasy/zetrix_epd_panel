/* components/data_types/include/fridge_memo_dto.h */
/**
 * @file fridge_memo_dto.h
 * @brief Fridge memo DTO types — shared type-only layer (design doc v1.2 §4.1/§7.1).
 *
 * Pure data used by fridge_memo_api (network) and fridge_memo_page (ui).
 * Field set mirrors the backend REST contract one-to-one; note/storage are
 * reserved (not rendered in P0).
 */
#ifndef DATA_TYPES_FRIDGE_MEMO_DTO_H_
#define DATA_TYPES_FRIDGE_MEMO_DTO_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FRIDGE_MEMO_MAX_ITEMS 64
#define FRIDGE_MEMO_ID_LEN 16
/* 40 bytes holds 13 CJK chars; the 24px title row renders <=12 CJK chars
 * (ui_text_fit_to_width), so contract-max longer names are truncated at
 * render anyway. */
#define FRIDGE_MEMO_NAME_LEN 40
/* 24 bytes holds 8 CJK chars; same render-truncation rationale as NAME_LEN. */
#define FRIDGE_MEMO_QTY_LEN 24
#define FRIDGE_MEMO_DATE_LEN 11 /* "YYYY-MM-DD" + NUL */
#define FRIDGE_MEMO_NOTE_LEN 64
#define FRIDGE_MEMO_STORAGE_LEN 8
#define FRIDGE_MEMO_UPDATED_LEN 24

/** Derived display status (device-side, computed at render time). */
typedef enum {
    FRIDGE_MEMO_STATUS_UNKNOWN = 0, /* no expires_at */
    FRIDGE_MEMO_STATUS_OK, /* remaining > 2 days */
    FRIDGE_MEMO_STATUS_NEAR, /* 0 <= remaining <= 2 days (临期, yellow) */
    FRIDGE_MEMO_STATUS_EXPIRED, /* remaining < 0 (red) */
} fridge_memo_status_t;

typedef struct {
    char id[FRIDGE_MEMO_ID_LEN];
    char name[FRIDGE_MEMO_NAME_LEN];
    char quantity[FRIDGE_MEMO_QTY_LEN];
    char added_at[FRIDGE_MEMO_DATE_LEN]; /* ISO date */
    char expires_at[FRIDGE_MEMO_DATE_LEN]; /* "" = no expiry */
    char note[FRIDGE_MEMO_NOTE_LEN];
    char storage[FRIDGE_MEMO_STORAGE_LEN]; /* "fridge"/"freezer", reserved */
} fridge_memo_item_t;

/** Authoritative full-list snapshot (backend is the single source of truth). */
typedef struct {
    fridge_memo_item_t items[FRIDGE_MEMO_MAX_ITEMS];
    int count; /* >64 backend entries are truncated to the 64 most urgent AFTER sort */
    char updated_at[FRIDGE_MEMO_UPDATED_LEN];
} fridge_memo_snapshot_t;

#ifdef __cplusplus
}
#endif

#endif /* DATA_TYPES_FRIDGE_MEMO_DTO_H_ */
