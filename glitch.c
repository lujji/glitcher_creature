#include "glitch.h"
#include <stdio.h>
#include <string.h>
#include <hardware/structs/hstx_ctrl.h>
#include <hardware/structs/hstx_fifo.h>
#include <hardware/sync.h>
#include <hardware/pio.h>
#include <hardware/clocks.h>
#include <hardware/gpio.h>
#include <hardware/dma.h>
#include <hardware/structs/bus_ctrl.h>
#include <pico/multicore.h>
#include "glitch.pio.h"
#include "config.h"

#define BITSTREAM_BUFFER_LEN    8192
#define BITSTREAM_SEQ_LEN       1024
#define SWITCHER_SEQ_LEN        1024
static_assert(SWITCHER_SEQ_LEN % 2 == 0, "SWITCHER_SEQ_LEN must be even");

#define FIRST_HSTX_PIN          12
static_assert(GLITCH_OUT_PIN >= FIRST_HSTX_PIN && GLITCH_OUT_PIN < FIRST_HSTX_PIN + 8 - 1,
    "GLITCH_OUT_PIN must be an HSTX-capable pin (12-19). 2 pins are used for DDR, pin 19 is not available.");
static_assert(GLITCH_OUT_PIN + GLITCH_OUT_COUNT - 1 < FIRST_HSTX_PIN + 8,
    "GLITCH_OUT_PIN + GLITCH_OUT_COUNT must be an HSTX-capable pin");

enum {
    GO,
    ABORT,
};

struct bitstream_seq {
    uint32_t buf[BITSTREAM_BUFFER_LEN];
    uint32_t count[BITSTREAM_SEQ_LEN];
    uint32_t buf_idx;
    uint32_t count_idx;
    uint32_t len;
    uint32_t *ptr;
    unsigned int pio_sm;
    unsigned int pio_offset;
    int dma_chan_a;
    int dma_chan_b;
    uint8_t zeros;
};

struct switcher_seq {
    uint32_t buf[SWITCHER_SEQ_LEN];
    uint32_t idx;
    unsigned int pio_sm;
    unsigned int pio_offset;
    int dma_chan;
};

static struct glitch_config cfg = {
    .rising_edge = true,
    .trigger_edges = 0,
    .zeros_first = false
};

static PIO pio;
static struct bitstream_seq bitstream;
static struct switcher_seq switcher;
static uint32_t dev_null;
static volatile uint32_t err_flag;

static void bitstream_dma_init() {
    dma_channel_config ch;

    /* channel a */
    ch = dma_channel_get_default_config(bitstream.dma_chan_a);
    channel_config_set_transfer_data_size(&ch, DMA_SIZE_32);
    channel_config_set_write_increment(&ch, false);
    channel_config_set_read_increment(&ch, false);
    channel_config_set_dreq(&ch, pio_get_dreq(pio, bitstream.pio_sm, true));
    channel_config_set_chain_to(&ch, bitstream.dma_chan_b);
    dma_channel_configure(bitstream.dma_chan_a, &ch, &pio->txf[bitstream.pio_sm], NULL, 0, false);

    /* channel b */
    ch = dma_channel_get_default_config(bitstream.dma_chan_b);
    channel_config_set_transfer_data_size(&ch, DMA_SIZE_32);
    channel_config_set_write_increment(&ch, false);
    channel_config_set_read_increment(&ch, true);
    channel_config_set_dreq(&ch, pio_get_dreq(pio, bitstream.pio_sm, true));
    channel_config_set_chain_to(&ch, bitstream.dma_chan_a);
    dma_channel_configure(bitstream.dma_chan_b, &ch, &pio->txf[bitstream.pio_sm], NULL, 0, false);

    /* configure transfer */
    bitstream.zeros = cfg.zeros_first;
    bitstream.ptr = bitstream.buf;
    bitstream.count_idx = 2;
    const int ch_pattern = bitstream.zeros ? bitstream.dma_chan_b : bitstream.dma_chan_a;
    const int ch_zeros = bitstream.zeros ? bitstream.dma_chan_a : bitstream.dma_chan_b;

    /* channel a */
    dma_hw->ch[ch_pattern].al1_ctrl |= DMA_CH0_CTRL_TRIG_INCR_READ_BITS;
    dma_channel_set_read_addr(ch_pattern, bitstream.ptr, false);
    dma_channel_set_transfer_count(ch_pattern, bitstream.count[bitstream.zeros ? 1 : 0], false);

    /* channel b */
    dma_hw->ch[ch_zeros].al1_ctrl &= ~DMA_CH0_CTRL_TRIG_INCR_READ_BITS;
    dma_channel_set_read_addr(ch_zeros, &dev_null, false);
    dma_channel_set_transfer_count(ch_zeros, bitstream.count[bitstream.zeros ? 0 : 1], false);

    if (bitstream.len <= 2) {
        /* no need to chain */
        const int last_ch = bitstream.len == 1 ? bitstream.dma_chan_a : bitstream.dma_chan_b;
        dma_hw->ch[last_ch].al1_ctrl &= ~DMA_CH0_CTRL_TRIG_CHAIN_TO_BITS;
        dma_hw->ch[last_ch].al1_ctrl |= (last_ch << DMA_CH0_CTRL_TRIG_CHAIN_TO_LSB);

        dma_channel_set_irq0_enabled(bitstream.dma_chan_a, false);
        dma_channel_set_irq0_enabled(bitstream.dma_chan_b, false);
    } else {
        dma_channel_set_irq0_enabled(bitstream.dma_chan_a, true);
        dma_channel_set_irq0_enabled(bitstream.dma_chan_b, true);
    }
}

static bool switcher_dma_init() {
    /* nothing to stream? */
    if (switcher.idx < 2)
        return false;

    /* channel */
    dma_channel_config ch = dma_channel_get_default_config(switcher.dma_chan);
    channel_config_set_transfer_data_size(&ch, DMA_SIZE_32);
    channel_config_set_write_increment(&ch, false);
    channel_config_set_read_increment(&ch, true);
    channel_config_set_dreq(&ch, pio_get_dreq(pio, switcher.pio_sm, true));
    dma_channel_configure(switcher.dma_chan, &ch, &pio->txf[switcher.pio_sm],
        switcher.buf + 1, switcher.idx - 1, false);

    // dma_channel_set_irq0_enabled(switcher.dma_chan, false);
    return true;
}

static void dma_abort() {
    const uint32_t mask = (1 << bitstream.dma_chan_a) | (1 << bitstream.dma_chan_b) | (1 << switcher.dma_chan);

    /* RP2350-E5: clear the EN bit of both the aborted channel and any channel it chains to */
    hw_clear_bits(&dma_hw->inte0, mask);
    hw_clear_bits(&dma_hw->ch[bitstream.dma_chan_a].al1_ctrl, DMA_CH0_CTRL_TRIG_EN_BITS);
    hw_clear_bits(&dma_hw->ch[bitstream.dma_chan_b].al1_ctrl, DMA_CH0_CTRL_TRIG_EN_BITS);
    hw_clear_bits(&dma_hw->ch[switcher.dma_chan].al1_ctrl, DMA_CH0_CTRL_TRIG_EN_BITS);

    dma_hw->abort = mask;
    while (dma_hw->abort & mask)
        tight_loop_contents();

    dma_hw->ints0 = mask;
}

static void hstx_init() {
    clock_configure_undivided(clk_hstx, 0,
        CLOCKS_CLK_HSTX_CTRL_AUXSRC_VALUE_CLK_SYS, SYS_CLK_HZ);

    /**
     * Initialize HSTX in PIO-coupled DDR mode, shift out MSB first
     *
     * PIO <-> HSTX mapping
     * 16 -> 28
     * 17 -> 29
     */
    for (int i = 0; i < GLITCH_OUT_COUNT; i++) {
        hstx_ctrl_hw->bit[GLITCH_OUT_PIN - FIRST_HSTX_PIN + i] =
            ((GLITCH_OUT_PIN + FIRST_HSTX_PIN + 1) << HSTX_CTRL_BIT0_SEL_P_LSB) |
            ((GLITCH_OUT_PIN + FIRST_HSTX_PIN) << HSTX_CTRL_BIT0_SEL_N_LSB);

        gpio_set_drive_strength(GLITCH_OUT_PIN + i, GPIO_DRIVE_STRENGTH_12MA);
        gpio_set_slew_rate(GLITCH_OUT_PIN + i, GPIO_SLEW_RATE_FAST);
        gpio_set_function(GLITCH_OUT_PIN + i, GPIO_FUNC_HSTX);
    }

    hstx_ctrl_hw->csr =
        HSTX_CTRL_CSR_EN_BITS |
        (pio_get_index(pio) << HSTX_CTRL_CSR_COUPLED_SEL_LSB) |
        (1u << HSTX_CTRL_CSR_COUPLED_MODE_LSB);
}

static void pio_init() {
    hard_assert(pio_claim_free_sm_and_add_program_for_gpio_range(&glitch_program,
        &pio, &bitstream.pio_sm, &bitstream.pio_offset, GLITCH_OUT_PIN, 1, true));
    switcher.pio_sm = pio_claim_unused_sm(pio, true);
    switcher.pio_offset = pio_add_program(pio, &switcher_program);

    glitch_disarm();
    hstx_init();
}

static void pio_deinit() {
    pio_remove_program_and_unclaim_sm(&glitch_program, pio, bitstream.pio_sm, bitstream.pio_offset);
    pio_remove_program_and_unclaim_sm(&switcher_program, pio, switcher.pio_sm, switcher.pio_offset);
}

/**
 * check if pattern-zeros blocks alternate
 */
static bool sequence_is_valid() {
    if (bitstream.len == 0)
        return false;

    uint64_t words = 0;
    for (uint32_t i = cfg.zeros_first ? 1 : 0; i < bitstream.len; i += 2)
        words += bitstream.count[i];
    return words <= bitstream.buf_idx;
}

static void __scratch_x("") dma_isr() {
    int idle_chan;
    if (dma_hw->ints0 & (1 << bitstream.dma_chan_a)) {
        dma_hw->ints0 = (1 << bitstream.dma_chan_a);
        idle_chan = bitstream.dma_chan_a;
    } else {
        dma_hw->ints0 = (1 << bitstream.dma_chan_b);
        idle_chan = bitstream.dma_chan_b;
    }

    /* reconfigure idle channel */
    uint32_t ctrl = dma_hw->ch[idle_chan].al1_ctrl;
    if (bitstream.zeros) {
        bitstream.zeros = 0;
        ctrl &= ~DMA_CH0_CTRL_TRIG_INCR_READ_BITS; // disable read increment
        dma_hw->ch[idle_chan].read_addr = (uint32_t) &dev_null;
    } else {
        bitstream.zeros = 1;
        bitstream.ptr += bitstream.count[bitstream.count_idx - 2];
        ctrl |= DMA_CH0_CTRL_TRIG_INCR_READ_BITS; // increment read
        dma_hw->ch[idle_chan].read_addr = (uint32_t) bitstream.ptr;
    }

    /* last two blocks - disable chaining and interrupts */
    if (bitstream.count_idx >= bitstream.len - 1) {
        ctrl = (ctrl & ~DMA_CH0_CTRL_TRIG_CHAIN_TO_BITS) |
               (idle_chan << DMA_CH0_CTRL_TRIG_CHAIN_TO_LSB);

        /* disable interrupts */
        dma_hw->inte0 &= ~((1 << bitstream.dma_chan_a) | (1 << bitstream.dma_chan_b));
    }

    dma_hw->ch[idle_chan].al1_ctrl = ctrl;
    dma_hw->ch[idle_chan].transfer_count = bitstream.count[bitstream.count_idx];

    // if (bitstream.count[bitstream.count_idx] == 0) err_flag = 1;
    bitstream.count_idx++;
}

static void glitch_loop() {
    irq_set_exclusive_handler(DMA_IRQ_0, dma_isr);
    irq_set_enabled(DMA_IRQ_0, true);

    bus_ctrl_hw->priority = BUSCTRL_BUS_PRIORITY_DMA_W_BITS | BUSCTRL_BUS_PRIORITY_DMA_R_BITS;

    while (1) {
        if (err_flag) {
            printf("Error: bitstream count was 0!\n");
            err_flag = 0;
        }

        uint32_t g = multicore_fifo_pop_blocking(); // multicore_fifo_rvalid
        (void) g;

        /* initialize DMA */
        dma_abort();
        bitstream_dma_init();
        uint32_t chan_mask = 1 << bitstream.dma_chan_a;
        if (switcher_dma_init())
            chan_mask |= 1 << switcher.dma_chan;

        /* prime PIOs */
        pio_sm_clear_fifos(pio, bitstream.pio_sm);
        pio_sm_clear_fifos(pio, switcher.pio_sm);
        pio_sm_put(pio, bitstream.pio_sm, cfg.trigger_edges);
        pio_sm_put(pio, switcher.pio_sm, cfg.trigger_edges);
        pio_sm_put(pio, switcher.pio_sm, switcher.buf[0]); // set initial state

        /* fire */
        dma_start_channel_mask(chan_mask);
        glitch_pio_enable(pio, bitstream.pio_sm, switcher.pio_sm);
    }
}

void glitch_init_sequence(bool zeros_first) {
    cfg.zeros_first = zeros_first;
    bitstream.len = 0;
    bitstream.buf_idx = 0;
    switcher.idx = 0;
    switcher.buf[0] = 1; // initial state
}

bool glitch_push_pattern(uint32_t pattern) {
    if (bitstream.buf_idx >= sizeof(bitstream.buf) / sizeof(bitstream.buf[0]))
        return false;

    bitstream.buf[bitstream.buf_idx++] = pattern;
    return true;
}

bool glitch_push_duration(uint32_t duration) {
    if (duration < 32 || bitstream.len >= sizeof(bitstream.count) / sizeof(bitstream.count[0]))
        return false;

    bitstream.count[bitstream.len++] = duration;
    return true;
}

bool glitch_push_switcher(uint32_t mask, uint32_t duration) {
    if (switcher.idx >= sizeof(switcher.buf) / sizeof(switcher.buf[0]))
        return false;

    switcher.buf[switcher.idx++] = mask;
    switcher.buf[switcher.idx++] = duration;

    return true;
}

/* this is only for testing from cli */
bool glitch_set_pulse(uint32_t holdoff, uint32_t width, uint32_t pattern) {
    if (width == 0 || width > BITSTREAM_BUFFER_LEN * 32)
        return false;

    const uint32_t shift = holdoff % 32;
    const uint32_t end = shift + width;
    const uint32_t last = (end - 1) / 32;
    const uint32_t zero_words = holdoff / 32;
    const uint32_t pattern_words = last + 2;

    if (pattern_words > BITSTREAM_BUFFER_LEN)
        return false;

    if (glitch_get_trigger_state() == GLITCH_ARMED)
        return false;

    if (glitch_get_trigger_state() == GLITCH_TRIGGERED)
        glitch_disarm();

    /* rotate until pattern bit 0 is the first bit after holdoff */
    const uint32_t rot = shift ? (pattern >> shift) | (pattern << (32 - shift)) : pattern;
    const uint32_t head = rot & (0xFFFFFFFF >> shift);
    const uint32_t tail = 0xFFFFFFFF << (31 - (end - 1) % 32);

    for (uint32_t i = 0; i < pattern_words; i++)
        bitstream.buf[i] = i < last ? rot : 0;

    if (last) {
        bitstream.buf[0] = head;
        bitstream.buf[last] = rot & tail;
    } else {
        bitstream.buf[0] = head & tail;
    }

    bitstream.buf_idx = pattern_words;
    bitstream.len = 0; // <= 2 blocks

    if (zero_words > 0) {
        cfg.zeros_first = true;
        bitstream.count[bitstream.len++] = zero_words;
    } else {
        cfg.zeros_first = false;
    }

    bitstream.count[bitstream.len++] = pattern_words;
    return true;
}

void glitch_init() {
    bitstream.dma_chan_a = dma_claim_unused_channel(true);
    bitstream.dma_chan_b = dma_claim_unused_channel(true);
    switcher.dma_chan = dma_claim_unused_channel(true);

    pio_init();
    multicore_launch_core1(glitch_loop);
}

void glitch_deinit() {
    pio_deinit();
    multicore_reset_core1();
}

enum GLITCH_STATE glitch_get_trigger_state() {
    uint8_t pc = pio_sm_get_pc(pio, bitstream.pio_sm) - bitstream.pio_offset;

    if (pc < glitch_offset_trigger)
        return GLITCH_IDLE;

    if (pc < glitch_offset_loop)
        return GLITCH_ARMED;

    return GLITCH_TRIGGERED;
}

bool glitch_arm() {
    if ((glitch_get_trigger_state() != GLITCH_IDLE) || !sequence_is_valid())
        return false;

    multicore_fifo_push_blocking(GO);

    /* wait for SMs to preload data */
    while (pio_sm_get_pc(pio, bitstream.pio_sm) < bitstream.pio_offset + glitch_offset_trigger)
        tight_loop_contents();
    while(pio_sm_get_pc(pio, switcher.pio_sm) < switcher.pio_offset + switcher_offset_trigger)
        tight_loop_contents();

    return true;
}

void glitch_disarm() {
    pio_set_sm_mask_enabled(pio, (1 << bitstream.pio_sm) | (1 << switcher.pio_sm), false);

    /* force output off */
    pio_sm_set_set_pins(pio, bitstream.pio_sm, GLITCH_OUT_PIN, 2);
    pio_sm_exec(pio, bitstream.pio_sm, pio_encode_set(pio_pins, 0));

    dma_abort();

    glitch_program_init(pio, bitstream.pio_sm, bitstream.pio_offset, cfg.rising_edge);
    glitch_switcher_program_init(pio, switcher.pio_sm, switcher.pio_offset, cfg.rising_edge);
    // pio_sm_set_pins_with_mask(pio, switcher.pio_sm, 0, ((1 << SWITCH_OUT_COUNT) - 1) << SWITCH_OUT_PIN);
}

void glitch_set_trigger_nth_edge(uint32_t edges) {
    cfg.trigger_edges = edges;
}

void glitch_set_trigger_polarity(bool rising_edge) {
    cfg.rising_edge = rising_edge;
    pio_deinit();
    pio_init();
}

bool glitch_force_trigger() {
    glitch_disarm();

    if (!glitch_arm())
        return false;

    glitch_pio_force_trigger(pio, bitstream.pio_sm, bitstream.pio_offset,
        switcher.pio_sm, switcher.pio_offset);

    return true;
}

void glitch_get_settings(struct glitch_config *out) {
    if (out)
        memcpy(out, &cfg, sizeof(struct glitch_config));
}

void glitch_print_sequence() {
    printf("seq len: %lu\n", bitstream.len);
    for (uint32_t i = 0; i < bitstream.buf_idx; i++) {
        printf("bitstream[%lu] = 0x%08lX\n", i, bitstream.buf[i]);
    }
}
