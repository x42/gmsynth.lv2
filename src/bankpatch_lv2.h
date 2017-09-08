#define LV2_BANKPATCH_URI "http://ardour.org/lv2/bankpatch"
#define LV2_BANKPATCH_PREFIX LV2_BANKPATCH_URI "#"
#define LV2_BANKPATCH__notify LV2_BANKPATCH_PREFIX "notify"

typedef void* LV2_BankPatch_Handle;

/** a LV2 Feature provided by the Host to the plugin */
typedef struct {
	/** Opaque host data */
	LV2_BankPatch_Handle handle;
	/** Info from plugin's run(), notify host that bank/program changed */
	void (*notify)(LV2_BankPatch_Handle handle, uint8_t channel, uint32_t bank, uint8_t pgm);
} LV2_BankPatch;
