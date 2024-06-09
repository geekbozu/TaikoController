#include <stdio.h>
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include <hardware/adc.h>
#include <hardware/dma.h>
#include "pico/util/queue.h"
#include <hardware/regs/dreq.h>
#include <stdio.h>
#include <stdint.h>
#include <pico/platform.h>
#include "input_queue.h"
#include "core1.h"

#define CPU_COUNT_INTERVAL_MS 100
#define CPU_COUNT_INTERVAL_US (CPU_COUNT_INTERVAL_MS*1000.0f)

uint8_t adc_vals[5] = {0, 1, 2, 3, 4};
float adc_smooth_vals[5] = {0, 1, 2, 3, 4};
uint8_t *data_ptr[1] = {adc_vals}; // Pointer to an address is required for
                                   // the reinitialization DMA channel.
                                   // Recall that DMA channels are basically
                                   // operating on arrays of data moving their
                                   // contents between locations.

volatile int polling_time_ms = 2;

float dsp_ema_f(float in, float average, float alpha)
{
    // 
    return in * (alpha) + average * (1 - alpha);
}

void core1_entry()
{
    // Setup ADC.
    printf("Second Core Started\n");
    adc_gpio_init(26);
    adc_gpio_init(27);
    adc_gpio_init(28);
    adc_gpio_init(29);
    adc_init();
    adc_set_temp_sensor_enabled(true); // Enable internal temperature sensor.
    adc_set_clkdiv(0);                 // Run at max speed.
    adc_set_round_robin(0x1f);         // Enable round-robin sampling of all 5 inputs.
    adc_select_input(0);               // Set starting ADC channel for round-robin mode.
    adc_fifo_setup(
        true,  // Write each completed conversion to the sample FIFO
        true,  // Enable DMA data request (DREQ)
        1,     // Assert DREQ (and IRQ) at least 1 sample present
        false, // Omit ERR bit (bit 15) since we have 8 bit reads.
        true   // shift each sample to 8 bits when pushing to FIFO
    );
    adc_fifo_drain();

    // Get two open DMA channels.
    // samp_chan will sample the adc, paced by DREQ_ADC and chain to ctrl_chan.
    // ctrl_chan will reconfigure & retrigger samp_chan when samp_chan finishes.
    int samp_chan = dma_claim_unused_channel(true);
    int ctrl_chan = dma_claim_unused_channel(true);
    dma_channel_config samp_conf = dma_channel_get_default_config(samp_chan);
    dma_channel_config ctrl_conf = dma_channel_get_default_config(ctrl_chan);

    // Setup Sample Channel.
    channel_config_set_transfer_data_size(&samp_conf, DMA_SIZE_8);
    channel_config_set_read_increment(&samp_conf, false); // read from adc FIFO reg.
    channel_config_set_write_increment(&samp_conf, true);
    channel_config_set_irq_quiet(&samp_conf, true);
    channel_config_set_dreq(&samp_conf, DREQ_ADC); // pace data according to ADC
    channel_config_set_chain_to(&samp_conf, ctrl_chan);
    channel_config_set_enable(&samp_conf, true);
    // Apply samp_chan configuration.
    dma_channel_configure(
        samp_chan, // Channel to be configured
        &samp_conf,
        NULL,               // write (dst) address will be loaded by ctrl_chan.
        &adc_hw->fifo,      // read (source) address. Does not change.
        count_of(adc_vals), // Number of word transfers.
        false               // Don't Start immediately.
    );

    // Setup Reconfiguration Channel
    // This channel will Write the starting address to the write address
    // "trigger" register, which will restart the DMA Sample Channel.
    channel_config_set_transfer_data_size(&ctrl_conf, DMA_SIZE_32);
    channel_config_set_read_increment(&ctrl_conf, false); // read a single uint32.
    channel_config_set_write_increment(&ctrl_conf, false);
    channel_config_set_irq_quiet(&ctrl_conf, true);
    channel_config_set_dreq(&ctrl_conf, DREQ_FORCE); // Go as fast as possible.
    channel_config_set_enable(&ctrl_conf, true);
    // Apply reconfig channel configuration.
    dma_channel_configure(
        ctrl_chan, // Channel to be configured
        &ctrl_conf,
        &dma_hw->ch[samp_chan].al2_write_addr_trig, // dst address. Writing here retriggers samp_chan.
        data_ptr,                                   // Read (src) address is a single array with the starting address.
        1,                                          // Number of word transfers.
        false                                       // Don't Start immediately.
    );
    dma_channel_start(ctrl_chan);
    adc_run(true); // Kick off the ADC in free-running mode.
    printf("ADC INITED\n");

    absolute_time_t polling_rate = make_timeout_time_ms(17);
    absolute_time_t sample_timer = make_timeout_time_us(100);
    absolute_time_t cpu_usage_timer = make_timeout_time_ms(CPU_COUNT_INTERVAL_MS);
    uint64_t total_cpu_time = 0;
    while (true)
    {
        absolute_time_t current_time = get_absolute_time();

        if(absolute_time_diff_us(cpu_usage_timer, current_time) > 0)
        {
            float percent = total_cpu_time/(float)CPU_COUNT_INTERVAL_US*100;
            printf("Core1 Used ~%.0f%% CPU  count: %llu/%1.0f\n",percent,total_cpu_time,CPU_COUNT_INTERVAL_US);
            total_cpu_time = 0;
            cpu_usage_timer = make_timeout_time_ms(CPU_COUNT_INTERVAL_MS);
        }
        if (absolute_time_diff_us(sample_timer, current_time) > 0)
        {
            uint64_t count = time_us_64();
            for (int i = 0; i < 5; i++)
            {
                float val = adc_vals[i] / 256;

                if (val > adc_smooth_vals[i])
                {
                    adc_smooth_vals[i] = adc_vals[i];
                }

                adc_smooth_vals[i] = dsp_ema_f(adc_vals[i], adc_smooth_vals[i], .01);
                sample_timer = make_timeout_time_us(20);
            }
            total_cpu_time = total_cpu_time + ( time_us_64() - count) ;
        }

        if (absolute_time_diff_us(polling_rate, current_time) > 0)
        {
            uint64_t count = time_us_64();
            struct queueItem item;
            // Sum adc values
            item.r_ka = adc_smooth_vals[0];
            item.r_don = adc_smooth_vals[1];
            item.l_don = adc_smooth_vals[2];
            item.l_ka = adc_smooth_vals[3];
            queue_try_add(&queue, &item);
            polling_rate = make_timeout_time_ms(polling_time_ms);
            total_cpu_time = total_cpu_time + ( time_us_64() - count) ;
        }
    }
}