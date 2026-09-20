/****************************************************************************
 * vendor/allwinner/chips/f1c100s/hardware/f1c100s_audio.h
 *
 * Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.  The
 * ASF licenses this file to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance with the
 * License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.  See the
 * License for the specific language governing permissions and limitations
 * under the License.
 *
 ****************************************************************************/

#ifndef __VENDOR_ALLWINNER_CHIPS_F1C100S_HARDWARE_F1C100S_AUDIO_H
#define __VENDOR_ALLWINNER_CHIPS_F1C100S_HARDWARE_F1C100S_AUDIO_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>
#include "chip.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* Audio Codec Register Offsets *********************************************/

#define F1C100S_DAC_DPC_OFFSET          0x0000 /* DAC Digital Part Control */
#define F1C100S_DAC_FIFOC_OFFSET        0x0004 /* DAC FIFO Control */
#define F1C100S_DAC_FIFOS_OFFSET        0x0008 /* DAC FIFO Status */
#define F1C100S_DAC_TXDATA_OFFSET       0x000c /* DAC TX Data */
#define F1C100S_ADC_FIFOC_OFFSET        0x0010 /* ADC FIFO Control */
#define F1C100S_ADC_FIFOS_OFFSET        0x0014 /* ADC FIFO Status */
#define F1C100S_ADC_RXDATA_OFFSET       0x0018 /* ADC RX Data */
#define F1C100S_DAC_MIXER_CTRL_OFFSET   0x0020 /* DAC Mixer Control */
#define F1C100S_ADC_MIXER_CTRL_OFFSET   0x0024 /* ADC Mixer Control */
#define F1C100S_ADDA_TUNE_OFFSET        0x0028 /* ADDA Tuning */
#define F1C100S_BIAS_CAL_CTRL0_OFFSET   0x002c /* Bias & DA16 Calibration Control 0 */
#define F1C100S_BIAS_CAL_CTRL1_OFFSET   0x0034 /* Bias & DA16 Calibration Control 1 */

/* CCU Audio Clock Registers (CCU Base: 0x01c20000) *************************/

#define F1C100S_CCU_PLL_AUDIO_CTRL      (0x01c20000 + 0x008)
#define F1C100S_CCU_CODEC_CLK           (0x01c20000 + 0x0140)

#define CCU_PLL_AUDIO_ENABLE            (1 << 31)
#define CCU_PLL_AUDIO_LOCK              (1 << 28)
#define CCU_CODEC_CLK_ENABLE            (1 << 31)

/* Register Bit Definitions *************************************************/

/* DAC Digital Part Control (DAC_DPC) */
#define DAC_DPC_EN_DA                   (1 << 31) /* Digital Audio Enable */
#define DAC_DPC_HPF_EN                  (1 << 18) /* High Pass Filter Enable */
#define DAC_DPC_DVOL_MASK               (0x3f << 12) /* Digital Volume Mask */
#define DAC_DPC_DVOL(n)                 ((n) << 12)

/* DAC FIFO Control (DAC_FIFOC) */
#define DAC_FIFOC_FS_MASK               (0x7 << 29) /* Sample Rate */
#define DAC_FIFOC_FS_48K                (0x0 << 29)
#define DAC_FIFOC_FS_32K                (0x1 << 29)
#define DAC_FIFOC_FS_24K                (0x2 << 29)
#define DAC_FIFOC_FS_16K                (0x3 << 29)
#define DAC_FIFOC_FS_12K                (0x4 << 29)
#define DAC_FIFOC_FS_8K                 (0x5 << 29)
#define DAC_FIFOC_FS_192K               (0x6 << 29)
#define DAC_FIFOC_FS_96K                (0x7 << 29)

#define DAC_FIFOC_FIFO_MODE_MASK        (0x3 << 24)
#define DAC_FIFOC_FIFO_MODE_24_31_8     (0x0 << 24)
#define DAC_FIFOC_FIFO_MODE_16_31_16    (0x0 << 24) /* Typo in datasheet? usually 0 */
#define DAC_FIFOC_FIFO_MODE_16_15_0     (0x1 << 24)

#define DAC_FIFOC_DRQ_CLR_CNT_MASK      (0x3 << 21)
#define DAC_FIFOC_TX_TRI_LEVEL_MASK     (0x7f << 8)
#define DAC_FIFOC_TX_TRI_LEVEL(n)       ((n) << 8)

#define DAC_FIFOC_MONO_EN               (1 << 6)
#define DAC_FIFOC_TX_SAMPLE_BITS        (1 << 5) /* 0:16bits, 1:24bits */
#define DAC_FIFOC_DAC_DRQ_EN            (1 << 4)
#define DAC_FIFOC_FIFO_FLUSH            (1 << 0)

/* DAC Analog & Output MIXER Control Register (DAC_MIXER_CTRL, 0x0020)
 * Reference: F1C200s User Manual Section 4.3.8.8 (p.167-168)
 */
#define DAC_MIXER_DACAREN               (1 << 31) /* Internal Analog Right DAC Enable */
#define DAC_MIXER_DACALEN               (1 << 30) /* Internal Analog Left DAC Enable */
#define DAC_MIXER_RMIXEN                (1 << 29) /* Right Analog Output Mixer Enable */
#define DAC_MIXER_LMIXEN                (1 << 28) /* Left Analog Output Mixer Enable */
#define DAC_MIXER_RHPPAMUTE             (1 << 27) /* Right Headphone PA Unmute (0: Mute, 1: Not mute) */
#define DAC_MIXER_LHPPAMUTE             (1 << 26) /* Left Headphone PA Unmute (0: Mute, 1: Not mute) */
#define DAC_MIXER_RHPIS                 (1 << 25) /* Right HP PA Input Source (0: DAC, 1: Mixer) */
#define DAC_MIXER_LHPIS                 (1 << 24) /* Left HP PA Input Source (0: DAC, 1: Mixer) */
#define DAC_MIXER_HPCOM_FC_MASK         (0x3 << 22) /* HPCOM Function Control Mask */
#define DAC_MIXER_HPCOM_FC(n)           (((n) & 0x3) << 22)
#define DAC_MIXER_COMPTEN               (1 << 21) /* HPCOM Output Protection Enable */
#define DAC_MIXER_RMIXMUTE_MASK         (0x1f << 16) /* Right Output Mixer Mute Mask (1: Not mute) */
#define DAC_MIXER_RMIXMUTE_MIC          (1 << 20) /* Bit 4: MICIN Boost stage */
#define DAC_MIXER_RMIXMUTE_LINEIN       (1 << 19) /* Bit 3: LINEIN */
#define DAC_MIXER_RMIXMUTE_FMINR        (1 << 18) /* Bit 2: FMINR */
#define DAC_MIXER_RMIXMUTE_RDAC         (1 << 17) /* Bit 1: Right channel DAC */
#define DAC_MIXER_RMIXMUTE_LDAC         (1 << 16) /* Bit 0: Left channel DAC */
#define DAC_MIXER_HPPAEN                (1 << 15) /* Right & Left Headphone PA Enable (1: Enable) */
#define DAC_MIXER_LMIXMUTE_MASK         (0x1f << 8)  /* Left Output Mixer Mute Mask (1: Not mute) */
#define DAC_MIXER_LMIXMUTE_MIC          (1 << 12) /* Bit 4: MICIN Boost stage */
#define DAC_MIXER_LMIXMUTE_LINEIN       (1 << 11) /* Bit 3: LINEIN */
#define DAC_MIXER_LMIXMUTE_FMINL        (1 << 10) /* Bit 2: FMINL */
#define DAC_MIXER_LMIXMUTE_LDAC         (1 << 9)  /* Bit 1: Left channel DAC */
#define DAC_MIXER_LMIXMUTE_RDAC         (1 << 8)  /* Bit 0: Right channel DAC */
#define DAC_MIXER_LTRNMUTE              (1 << 7)  /* Left HPOUT Negative To Right HPOUT Mute */
#define DAC_MIXER_RTLNMUTE              (1 << 6)  /* Right HPOUT Negative To Left HPOUT Mute */
#define DAC_MIXER_HPVOL_MASK            (0x3f << 0) /* Headphone Volume Control Mask */
#define DAC_MIXER_HPVOL(n)              (((n) & 0x3f) << 0)

/* ADC FIFO Control (ADC_FIFOC) */
#define ADC_FIFOC_FS_MASK               (0x7 << 29)
#define ADC_FIFOC_FS_48K                (0x0 << 29)
#define ADC_FIFOC_FS_32K                (0x1 << 29)
#define ADC_FIFOC_FS_24K                (0x2 << 29)
#define ADC_FIFOC_FS_16K                (0x3 << 29)
#define ADC_FIFOC_FS_12K                (0x4 << 29)
#define ADC_FIFOC_FS_8K                 (0x5 << 29)
#define ADC_FIFOC_FS_192K               (0x6 << 29)
#define ADC_FIFOC_FS_96K                (0x7 << 29)

#define ADC_FIFOC_EN_AD                 (1 << 28)
#define ADC_FIFOC_RX_FIFO_MODE          (1 << 24)
#define ADC_FIFOC_RX_TRI_LEVEL_MASK     (0x7f << 8)
#define ADC_FIFOC_RX_TRI_LEVEL(n)       ((n) << 8)
#define ADC_FIFOC_MONO_EN               (1 << 7)
#define ADC_FIFOC_RX_SAMPLE_BITS        (1 << 6) /* 0:16bits, 1:24bits */
#define ADC_FIFOC_ADC_DRQ_EN            (1 << 4)
#define ADC_FIFOC_FIFO_FLUSH            (1 << 0)

/* DAC FIFO Status (DAC_FIFOS) */
#define DAC_FIFOS_TXE_CNT_MASK          (0xff << 0)
#define DAC_FIFOS_TXE_INT               (1 << 3)

/* ADC FIFO Status (ADC_FIFOS) */
#define ADC_FIFOS_RXA_CNT_MASK          (0x3f << 0)
#define ADC_FIFOS_RXA_INT               (1 << 3)

/* ADC Analog and Input Mixer Control Register (ADC_MIXER_CTRL, 0x0024)
 * Reference: F1C200s User Manual Section 4.3.8.9 (p.169)
 */
#define ADC_MIXER_ADCEN                 (1 << 31) /* ADC Analog Enable (0: Disable, 1: Enable) */
#define ADC_MIXER_MICG_MASK             (0x7 << 24) /* MICIN BOOST to L/R mixer gain */
#define ADC_MIXER_MICG(n)               (((n) & 0x7) << 24)
#define ADC_MIXER_LINEINVOL_MASK        (0x7 << 21) /* LINEIN to L/R mixer gain */
#define ADC_MIXER_LINEINVOL(n)          (((n) & 0x7) << 21)
#define ADC_MIXER_ADCG_MASK             (0x7 << 16) /* ADC Input Gain */
#define ADC_MIXER_ADCG(n)               (((n) & 0x7) << 16)
#define ADC_MIXER_COSSLOPECTRL_MASK     (0x3 << 14) /* COS slope time control for Anti-pop */
#define ADC_MIXER_ADCMIXMUTE_MASK       (0x3f << 8) /* ADC Mixer Mute Control (1: Not mute) */
#define ADC_MIXER_ADCMIXMUTE_MIC        (1 << 13) /* Bit 5: MICIN Boost stage */
#define ADC_MIXER_ADCMIXMUTE_FMINL      (1 << 12) /* Bit 4: FMINL */
#define ADC_MIXER_ADCMIXMUTE_FMINR      (1 << 11) /* Bit 3: FMINR */
#define ADC_MIXER_ADCMIXMUTE_LINEIN     (1 << 10) /* Bit 2: LINEIN */
#define ADC_MIXER_ADCMIXMUTE_LMIX       (1 << 9)  /* Bit 1: Left output mixer */
#define ADC_MIXER_ADCMIXMUTE_RMIX       (1 << 8)  /* Bit 0: Right output mixer */
#define ADC_MIXER_PASPEEDSELECT         (1 << 7)  /* PA Speed Select (0: Normal, 1: Fast) */
#define ADC_MIXER_FMINLVOL_MASK         (0x7 << 4) /* FMINL/R to L/R mixer gain */
#define ADC_MIXER_FMINLVOL(n)           (((n) & 0x7) << 4)
#define ADC_MIXER_MIC_AMPEN             (1 << 3)  /* MIC Boost AMP Enable */
#define ADC_MIXER_MICBOOST_MASK         (0x7 << 0) /* MIC Boost AMP Gain Control */
#define ADC_MIXER_MICBOOST(n)           (((n) & 0x7) << 0)

/* Compatibility aliases */
#define ADC_MIXER_MIC1_EN               (ADC_MIXER_MIC_AMPEN | ADC_MIXER_ADCMIXMUTE_MIC)
#define ADC_MIXER_LINEIN_EN             ADC_MIXER_ADCMIXMUTE_LINEIN

/* Power-on Tuning & Calibration Magic Values (UM p.170-172) *****************/

#define ADDA_TUNE_MAGIC                 0x44555556 /* ADDA_TUNE_REG (0x28) power-on init */
#define BIAS_CAL_CTRL0_MAGIC            0x00000004 /* BIAS_DA16_CAL_CTRL0_REG (0x2c) power-on init */
#define BIAS_CAL_CTRL1_MAGIC            0x10000000 /* BIAS_DA16_CAL_CTRL1_REG (0x34) power-on init */

/* DMA DRQ Type for Audio Codec (from F1C100s datasheet) */
#define AUDIO_DMA_DRQ_TYPE              26  /* DRQ type for Audio Codec (shared DAC/ADC) */
#define AUDIO_DMA_CHAN_DAC              AUDIO_DMA_DRQ_TYPE
#define AUDIO_DMA_CHAN_ADC              AUDIO_DMA_DRQ_TYPE

/* Audio Buffer Configuration */
#define AUDIO_BUFFER_SIZE               8192  /* Buffer size for DMA */
#define AUDIO_NUM_BUFFERS               4     /* Number of buffers */


/****************************************************************************
 * Public Types
 ****************************************************************************/

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

#ifndef __ASSEMBLY__

#undef EXTERN
#if defined(__cplusplus)
#define EXTERN extern "C"
extern "C"
{
#else
#define EXTERN extern
#endif

/****************************************************************************
 * Name: f1c100s_audio_initialize
 *
 * Description:
 *   Initialize the Audio Codec hardware and return a lower-half driver
 *   instance.
 *
 * Input Parameters:
 *   None
 *
 * Returned Value:
 *   A pointer to the lower-half driver instance on success; NULL on failure.
 *
 ****************************************************************************/

struct audio_lowerhalf_s; /* Forward declaration */
FAR struct audio_lowerhalf_s *f1c100s_audio_initialize(void);

/****************************************************************************
 * Name: f1c100s_audio_uninitialize
 *
 * Description:
 *   Uninitialize the Audio Codec driver
 *
 * Input Parameters:
 *   dev - Audio lower-half driver instance
 *
 * Returned Value:
 *   OK on success; negative errno on failure
 *
 ****************************************************************************/

int f1c100s_audio_uninitialize(FAR struct audio_lowerhalf_s *dev);

#undef EXTERN
#if defined(__cplusplus)
}
#endif

#endif /* __ASSEMBLY__ */
#endif /* __VENDOR_ALLWINNER_CHIPS_F1C100S_HARDWARE_F1C100S_AUDIO_H */
