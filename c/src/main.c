#include <stdio.h>
#include "pico/stdlib.h"
#include <hardware/adc.h>
#include <hardware/dma.h>
#include <hardware/regs/dreq.h>
#include <stdio.h>
#include <stdint.h>

// Demo to continuously sample all ADC inputs and write them to a location in
// memory where they can be read. Datasheet calls this data "scattering."

// Note: According to the datasheet sec 2.5.1, DMA read and write addresses must
//  be pointers to an address.
// Note: According to the datasheet sec 2.5.1.1, the way to reinitialize a
//  channel with an incrementing (read or write) address would be to rewrite the
//  starting address before (or upon) restart.
//  Otherwise, "If READ_ADDR and WRITE_ADDR are not reprogrammed, the DMA will
//  use the current values as start addresses for the next transfer."

uint8_t adc_vals[5] = {0, 1, 2, 3, 4};
float adc_smooth_vals[5] = {0, 1, 2, 3, 4};
uint8_t* data_ptr[1] = {adc_vals}; // Pointer to an address is required for
                                   // the reinitialization DMA channel.
                                   // Recall that DMA channels are basically
                                   // operating on arrays of data moving their
                                   // contents between locations.
float dsp_ema_f(float in, float average, float alpha){
  
  return in * (alpha) + average * (1- alpha);
  
}
int main()
{
    stdio_usb_init();
    // stdio_set_translate_crlf(&stdio_usb, false); // Don't replace outgoing chars.
    while (!stdio_usb_connected()){} // Block until connection to serial port.
    sleep_ms(50);

    // Setup ADC.
    adc_gpio_init(26);
    adc_gpio_init(27);
    adc_gpio_init(28);
    adc_gpio_init(29);
    adc_init();
    adc_set_temp_sensor_enabled(true); // Enable internal temperature sensor.
    adc_set_clkdiv(0); // Run at max speed.
    adc_set_round_robin(0x1f); // Enable round-robin sampling of all 5 inputs.
    adc_select_input(0); // Set starting ADC channel for round-robin mode.
    adc_fifo_setup(
        true,    // Write each completed conversion to the sample FIFO
        true,    // Enable DMA data request (DREQ)
        1,       // Assert DREQ (and IRQ) at least 1 sample present
        false,   // Omit ERR bit (bit 15) since we have 8 bit reads.
        true     // shift each sample to 8 bits when pushing to FIFO
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
        samp_chan,          // Channel to be configured
        &samp_conf,
        NULL,            // write (dst) address will be loaded by ctrl_chan.
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
        ctrl_chan,  // Channel to be configured
        &ctrl_conf,
        &dma_hw->ch[samp_chan].al2_write_addr_trig, // dst address. Writing here retriggers samp_chan.
        data_ptr,   // Read (src) address is a single array with the starting address.
        1,          // Number of word transfers.
        false       // Don't Start immediately.
    );
    dma_channel_start(ctrl_chan);
    adc_run(true); // Kick off the ADC in free-running mode.

    // Read the latest data.

    absolute_time_t print_timer = make_timeout_time_ms(17);
    absolute_time_t sample_timer = make_timeout_time_us(100);

    while(true)
    {
        absolute_time_t current_time = get_absolute_time();
        if( absolute_time_diff_us(sample_timer,current_time) > 0 )
        { 
            for(int i = 0; i < 5;i++){
                float val = adc_vals[i]/256;

                if(val > adc_smooth_vals[i])
                {
                    adc_smooth_vals[i] = adc_vals[i];
                }
    
                adc_smooth_vals[i] = dsp_ema_f(adc_vals[i] ,adc_smooth_vals[i],.01);
                sample_timer = make_timeout_time_us(20);
            }
        }

        if( absolute_time_diff_us(print_timer,current_time) > 0 )
        {
            printf(">adc0:%1.4f\n>adc1:%1.4f\n",
                MAX(0,adc_smooth_vals[0]), MAX(0,adc_smooth_vals[1] - adc_smooth_vals[0]));
            print_timer = make_timeout_time_ms(2);
         }
    }
}
