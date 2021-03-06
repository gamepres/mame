// license:GPL-2.0+
// copyright-holders:Juergen Buchmueller, David Haywood
/***********************************************************************

    DECO Cassette System driver
    by Juergen Buchmueller
    with contributions by:
    David Widel
    Nicola Salmoria
    Aaron Giles
    Brian Troha
    Fabio Priuli
    Lord Nightmare
    The Dumping Union
    Team Japump!!!
    Hau
	Jean-Francois Del Nero
	Omar Cornut
	Game Preservation Society
	Joseph Redon

    The DECO cassette system consists of three PCBS in a card cage:
    Early boardset: (1980-1983) (proms unknown for this boardset, no schematics for this boardset)
    One DE-0069C-0 RMS-3 pcb with a 6502 processor, D8041C MCU (DECO Cassette control), two ay-3-8910s, and one 2708 eprom holding the audio bios. (audio, needs external amp and volume control)
    One DE-0068B-0 DSP-3 pcb with a 'DECO CPU-3' custom, two 2716 eproms. (main processor and bios, graphics, dipswitches?)
    One DE-0070C-0 BIO-3 pcb with an analog ADC0908 8-bit adc.
    One DE-0066B-0 card rack board that the other three boards plug into.
	This boardset has two versions : MD, known as "shokase" which is also the version sold out of Japan, and MT, known as "daikase" which is using bigger data tapes.
	The MT system isn't emulated yet.

    Later boardset: (1984 onward, schematic is dated October 1983)
    One DE-0097C-0 RMS-8 pcb with a 6502 processor, two ay-3-8910s, two eproms (2716 and 2732) plus one prom, and 48k worth of 4116 16kx1 DRAMs; the 6502 processor has its own 4K of SRAM. (audio processor and RAM, Main processor's dram, dipswitches)
    One DE-0096C-0 DSP-8 board with a 'DECO 222' custom on it (labeled '8049 // C10707-2') which appears to really be a 'cleverly' disguised 6502, and two proms, plus 4K of sram, and three hm2511-1 1kx1 srams. (main processor and graphics)
    One DE-0098C-0 B10-8 (BIO-8 on schematics) board with an 8041, an analog devices ADC0908 8-bit adc, and 4K of SRAM on it. (DECO Cassette control, inputs)
    One DE-0109C-0 card rack board that the other three boards plug into.

    The actual cassettes use a custom player hooked to the BIO board, and are roughly microcassette form factor, but are larger and will not fit in a conventional microcassette player.
    Each cassette has one track on it and is separated into clock and data by two Magtek IC in the player, for a form of synchronous serial.
	The data is stored in blocks with headers and CRC16 checksums.
	The first block contains information such as the region (A:Japan, B:USA, C:UK, D:Europe) and the total number of blocks left to read.
	The last physical block on the cassette is a dummy block not used by the system.

 ***********************************************************************/

#include "emu.h"
#include "cpu/m6502/m6502.h"
#include "cpu/mcs48/mcs48.h"
#include "includes/decocass.h"
#include "machine/decocass_tape.h"
#include "sound/ay8910.h"
#include "machine/deco222.h"

#define MASTER_CLOCK    XTAL_12MHz
#define HCLK            (MASTER_CLOCK/2)
#define HCLK1           (HCLK/2)
#define HCLK2           (HCLK1/2)
#define HCLK4           (HCLK2/2)


/***************************************************************************
 *
 *  swizzled mirror handlers
 *
 ***************************************************************************/

WRITE8_MEMBER(decocass_state::mirrorvideoram_w) { offset = ((offset >> 5) & 0x1f) | ((offset & 0x1f) << 5); decocass_fgvideoram_w(space, offset, data, mem_mask); }
WRITE8_MEMBER(decocass_state::mirrorcolorram_w) { offset = ((offset >> 5) & 0x1f) | ((offset & 0x1f) << 5); decocass_colorram_w(space, offset, data, mem_mask); }

READ8_MEMBER(decocass_state::mirrorvideoram_r)
{
	offset = ((offset >> 5) & 0x1f) | ((offset & 0x1f) << 5);
	return m_fgvideoram[offset];
}

READ8_MEMBER(decocass_state::mirrorcolorram_r)
{
	offset = ((offset >> 5) & 0x1f) | ((offset & 0x1f) << 5);
	return m_colorram[offset];
}


static ADDRESS_MAP_START( decocass_map, AS_PROGRAM, 8, decocass_state )
	AM_RANGE(0x0000, 0x5fff) AM_RAM AM_SHARE("rambase")
	AM_RANGE(0x6000, 0xbfff) AM_RAM_WRITE(decocass_charram_w) AM_SHARE("charram") /* still RMS3 RAM */
	AM_RANGE(0xc000, 0xc3ff) AM_RAM_WRITE(decocass_fgvideoram_w) AM_SHARE("fgvideoram")  /* DSP3 RAM */
	AM_RANGE(0xc400, 0xc7ff) AM_RAM_WRITE(decocass_colorram_w) AM_SHARE("colorram")
	AM_RANGE(0xc800, 0xcbff) AM_READWRITE(mirrorvideoram_r, mirrorvideoram_w)
	AM_RANGE(0xcc00, 0xcfff) AM_READWRITE(mirrorcolorram_r, mirrorcolorram_w)
	AM_RANGE(0xd000, 0xd7ff) AM_RAM_WRITE(decocass_tileram_w) AM_SHARE("tileram")
	AM_RANGE(0xd800, 0xdbff) AM_RAM_WRITE(decocass_objectram_w) AM_SHARE("objectram")
	AM_RANGE(0xe000, 0xe0ff) AM_RAM_WRITE(decocass_paletteram_w) AM_SHARE("paletteram")
	AM_RANGE(0xe300, 0xe300) AM_READ_PORT("DSW1") AM_WRITE(decocass_watchdog_count_w)
	AM_RANGE(0xe301, 0xe301) AM_READ_PORT("DSW2") AM_WRITE(decocass_watchdog_flip_w)
	AM_RANGE(0xe302, 0xe302) AM_WRITE(decocass_color_missiles_w)
	AM_RANGE(0xe400, 0xe400) AM_WRITE(decocass_reset_w)

/* BIO-3 board */
	AM_RANGE(0xe402, 0xe402) AM_WRITE(decocass_mode_set_w)      /* scroll mode regs + various enable regs */
	AM_RANGE(0xe403, 0xe403) AM_WRITE(decocass_back_h_shift_w)  /* back (both)  tilemap x scroll */
	AM_RANGE(0xe404, 0xe404) AM_WRITE(decocass_back_vl_shift_w) /* back (left)  (top@rot0) tilemap y scroll */
	AM_RANGE(0xe405, 0xe405) AM_WRITE(decocass_back_vr_shift_w) /* back (right) (bot@rot0) tilemap y scroll */
	AM_RANGE(0xe406, 0xe406) AM_WRITE(decocass_part_h_shift_w)  /* headlight */
	AM_RANGE(0xe407, 0xe407) AM_WRITE(decocass_part_v_shift_w)  /* headlight */

	AM_RANGE(0xe410, 0xe410) AM_WRITE(decocass_color_center_bot_w)
	AM_RANGE(0xe411, 0xe411) AM_WRITE(decocass_center_h_shift_space_w)
	AM_RANGE(0xe412, 0xe412) AM_WRITE(decocass_center_v_shift_w)
	AM_RANGE(0xe413, 0xe413) AM_WRITE(decocass_coin_counter_w)
	AM_RANGE(0xe414, 0xe414) AM_READWRITE(decocass_sound_command_main_r, decocass_sound_command_w)
	AM_RANGE(0xe415, 0xe416) AM_WRITE(decocass_quadrature_decoder_reset_w)
	AM_RANGE(0xe417, 0xe417) AM_WRITE(decocass_nmi_reset_w)
	AM_RANGE(0xe420, 0xe42f) AM_WRITE(decocass_adc_w)

	AM_RANGE(0xe500, 0xe5ff) AM_READWRITE(decocass_e5xx_r, decocass_e5xx_w) /* read data from 8041/status */

	AM_RANGE(0xe600, 0xe6ff) AM_READ(decocass_input_r)      /* inputs */
	AM_RANGE(0xe700, 0xe700) AM_READ(decocass_sound_data_r) /* read sound CPU data */
	AM_RANGE(0xe701, 0xe701) AM_READ(decocass_sound_ack_r)  /* read sound CPU ack status */

	AM_RANGE(0xf000, 0xffff) AM_ROM
ADDRESS_MAP_END

static ADDRESS_MAP_START( decocass_sound_map, AS_PROGRAM, 8, decocass_state )
	AM_RANGE(0x0000, 0x0fff) AM_RAM
	AM_RANGE(0x1000, 0x17ff) AM_READWRITE(decocass_sound_nmi_enable_r, decocass_sound_nmi_enable_w)
	AM_RANGE(0x1800, 0x1fff) AM_READWRITE(decocass_sound_data_ack_reset_r, decocass_sound_data_ack_reset_w)
	AM_RANGE(0x2000, 0x2fff) AM_DEVWRITE("ay1", ay8910_device, data_w)
	AM_RANGE(0x4000, 0x4fff) AM_DEVWRITE("ay1", ay8910_device, address_w)
	AM_RANGE(0x6000, 0x6fff) AM_DEVWRITE("ay2", ay8910_device, data_w)
	AM_RANGE(0x8000, 0x8fff) AM_DEVWRITE("ay2", ay8910_device, address_w)
	AM_RANGE(0xa000, 0xafff) AM_READ(decocass_sound_command_r)
	AM_RANGE(0xc000, 0xcfff) AM_WRITE(decocass_sound_data_w)
	AM_RANGE(0xf800, 0xffff) AM_ROM
ADDRESS_MAP_END


static ADDRESS_MAP_START( decocass_mcu_portmap, AS_IO, 8, decocass_state )
	AM_RANGE(MCS48_PORT_P1, MCS48_PORT_P1) AM_READWRITE(i8041_p1_r, i8041_p1_w)
	AM_RANGE(MCS48_PORT_P2, MCS48_PORT_P2) AM_READWRITE(i8041_p2_r, i8041_p2_w)
ADDRESS_MAP_END

static INPUT_PORTS_START( decocass )
	PORT_START("IN0")
	PORT_BIT( 0x01, IP_ACTIVE_HIGH,IPT_JOYSTICK_RIGHT )
	PORT_BIT( 0x02, IP_ACTIVE_HIGH,IPT_JOYSTICK_LEFT )
	PORT_BIT( 0x04, IP_ACTIVE_HIGH,IPT_JOYSTICK_UP )
	PORT_BIT( 0x08, IP_ACTIVE_HIGH,IPT_JOYSTICK_DOWN )
	PORT_BIT( 0x10, IP_ACTIVE_HIGH,IPT_BUTTON1 )
	PORT_BIT( 0x20, IP_ACTIVE_HIGH,IPT_BUTTON2 )
	PORT_BIT( 0x40, IP_ACTIVE_HIGH,IPT_UNUSED )
	PORT_BIT( 0x80, IP_ACTIVE_HIGH,IPT_UNUSED )

	PORT_START("IN1")
	PORT_BIT( 0x01, IP_ACTIVE_HIGH,IPT_JOYSTICK_RIGHT ) PORT_COCKTAIL
	PORT_BIT( 0x02, IP_ACTIVE_HIGH,IPT_JOYSTICK_LEFT ) PORT_COCKTAIL
	PORT_BIT( 0x04, IP_ACTIVE_HIGH,IPT_JOYSTICK_UP ) PORT_COCKTAIL
	PORT_BIT( 0x08, IP_ACTIVE_HIGH,IPT_JOYSTICK_DOWN ) PORT_COCKTAIL
	PORT_BIT( 0x10, IP_ACTIVE_HIGH,IPT_BUTTON1 ) PORT_COCKTAIL
	PORT_BIT( 0x20, IP_ACTIVE_HIGH,IPT_BUTTON2 ) PORT_COCKTAIL
	PORT_BIT( 0x40, IP_ACTIVE_HIGH,IPT_UNUSED )
	PORT_BIT( 0x80, IP_ACTIVE_HIGH,IPT_UNUSED )

	PORT_START("IN2")
	PORT_BIT( 0x01, IP_ACTIVE_HIGH,IPT_UNKNOWN )
	PORT_BIT( 0x02, IP_ACTIVE_HIGH,IPT_UNKNOWN )
	PORT_BIT( 0x04, IP_ACTIVE_HIGH,IPT_UNKNOWN )
	PORT_BIT( 0x08, IP_ACTIVE_HIGH,IPT_START1 ) PORT_IMPULSE(1)
	PORT_BIT( 0x10, IP_ACTIVE_HIGH,IPT_START2 ) PORT_IMPULSE(1)
	PORT_BIT( 0x20, IP_ACTIVE_HIGH,IPT_UNKNOWN )
	PORT_BIT( 0x40, IP_ACTIVE_LOW, IPT_COIN2 ) PORT_IMPULSE(1)
	PORT_BIT( 0x80, IP_ACTIVE_LOW, IPT_COIN1 ) PORT_IMPULSE(1)

	PORT_START("AN0")
	PORT_BIT( 0xff, 0x80, IPT_AD_STICK_X ) PORT_MINMAX(0x10,0xf0) PORT_SENSITIVITY(100) PORT_KEYDELTA(10) PORT_PLAYER(1)

	PORT_START("AN1")
	PORT_BIT( 0xff, 0x80, IPT_AD_STICK_Y ) PORT_MINMAX(0x10,0xf0) PORT_SENSITIVITY(100) PORT_KEYDELTA(10) PORT_PLAYER(1)

	PORT_START("AN2")
	PORT_BIT( 0xff, 0x80, IPT_AD_STICK_X ) PORT_MINMAX(0x10,0xf0) PORT_SENSITIVITY(100) PORT_KEYDELTA(10) PORT_PLAYER(2)

	PORT_START("AN3")
	PORT_BIT( 0xff, 0x80, IPT_AD_STICK_Y ) PORT_MINMAX(0x10,0xf0) PORT_SENSITIVITY(100) PORT_KEYDELTA(10) PORT_PLAYER(2)

	PORT_START("DSW1")
	PORT_DIPNAME( 0x03, 0x03, DEF_STR( Coin_A ) )                       PORT_DIPLOCATION("SW1:1,2")   /* Listed as "Game Charge" for "Table" and "Up Right RH selector" */
	PORT_DIPSETTING(    0x03, DEF_STR( 1C_1C ) )
	PORT_DIPSETTING(    0x02, DEF_STR( 1C_2C ) )
	PORT_DIPSETTING(    0x01, DEF_STR( 1C_3C ) )
	PORT_DIPSETTING(    0x00, DEF_STR( 2C_1C ) )
	PORT_DIPNAME( 0x0c, 0x0c, DEF_STR( Coin_B ) )                       PORT_DIPLOCATION("SW1:3,4")   /* Listed as "Game Charge" for "Up Right LH selector" */
	PORT_DIPSETTING(    0x0c, DEF_STR( 1C_1C ) )
	PORT_DIPSETTING(    0x08, DEF_STR( 1C_2C ) )
	PORT_DIPSETTING(    0x04, DEF_STR( 1C_3C ) )
	PORT_DIPSETTING(    0x00, DEF_STR( 2C_1C ) )
	PORT_DIPNAME( 0x30, 0x30, "Type of Tape" )                          PORT_DIPLOCATION("SW1:5,6")   /* Used by the "bios" */
	PORT_DIPSETTING(    0x30, "MD" )        /* Was listed as "Board Type" with this being "NEW" */
	PORT_DIPSETTING(    0x00, "MT" )        /* Was listed as "Board Type" with this being "OLD" */
	PORT_DIPNAME( 0x40, 0x40, "Control Panel" )                         PORT_DIPLOCATION("SW1:7")     /* Default is "Up Right" for non Japanese versions */
	PORT_DIPSETTING(    0x40, "Table" )
	PORT_DIPSETTING(    0x00, "Up Right" )
	PORT_BIT( 0x80, IP_ACTIVE_HIGH, IPT_CUSTOM) PORT_VBLANK("screen")        /* Listed as "Screen" > "Table" or "Up Right" */

	PORT_START("DSW2") /* Start with all Unknown as each can change per game, except for Country Code */
	PORT_DIPUNKNOWN_DIPLOC( 0x01, 0x01, "SW2:1")        /* Most Dipswitch Settings sheets show this as "Number of Players" (Lives) */
	PORT_DIPUNKNOWN_DIPLOC( 0x02, 0x02, "SW2:2")        /* Most Dipswitch Settings sheets show 2 & 3 as "Bonus Players" */
	PORT_DIPUNKNOWN_DIPLOC( 0x04, 0x04, "SW2:3")
	PORT_DIPUNKNOWN_DIPLOC( 0x08, 0x08, "SW2:4")        /* Most Dipswitch Settings sheets show 4 (with/without 5) as some form of Diffculty */
	PORT_DIPUNKNOWN_DIPLOC( 0x10, 0x10, "SW2:5")
	PORT_DIPNAME( 0xe0, 0xe0, "Country Code" )                          PORT_DIPLOCATION("SW2:6,7,8") /* Always Listed as "DON'T CHANGE" */
	PORT_DIPSETTING(    0xe0, "A" )
	PORT_DIPSETTING(    0xc0, "B" )
	PORT_DIPSETTING(    0xa0, "C" )
	PORT_DIPSETTING(    0x80, "D" )
	PORT_DIPSETTING(    0x60, "E" )
	PORT_DIPSETTING(    0x40, "F" )
INPUT_PORTS_END

static INPUT_PORTS_START( chwych0a )
	PORT_INCLUDE( decocass )

	PORT_MODIFY("DSW2")
	PORT_DIPNAME( 0x03, 0x03, "Number of Cars" )                        PORT_DIPLOCATION("SW2:1,2") /* Lives */
	PORT_DIPSETTING(    0x03, "3" )
	PORT_DIPSETTING(    0x02, "4" )
	PORT_DIPSETTING(    0x01, "5" )
	PORT_DIPSETTING(    0x00, "6" )
	PORT_DIPNAME( 0x0c, 0x0c, "Bonus Point" )                           PORT_DIPLOCATION("SW2:3,4") /* Extra Life */
	PORT_DIPSETTING(    0x0c, "3000" )
	PORT_DIPSETTING(    0x08, "5000" )
	PORT_DIPSETTING(    0x04, "7000" )
	PORT_DIPSETTING(    0x00, DEF_STR( None ) )
	PORT_DIPNAME( 0x10, 0x10, "Unused" )                                PORT_DIPLOCATION("SW2:5")
	PORT_DIPSETTING(    0x10, DEF_STR( Off ) )
	PORT_DIPSETTING(    0x00, DEF_STR( On ) )
	/* Switches 5 to 8 are shown as completly blank */
INPUT_PORTS_END

static INPUT_PORTS_START( cninjt0a )
	PORT_INCLUDE( decocass )

	PORT_MODIFY("DSW2")
	PORT_DIPNAME( 0x03, 0x03, "Number of Sight" )                       PORT_DIPLOCATION("SW2:1,2") /* Lives */
	PORT_DIPSETTING(    0x03, "3" )
	PORT_DIPSETTING(    0x02, "4" )
	PORT_DIPSETTING(    0x01, "5" )
	PORT_DIPSETTING(    0x00, "6" )
	PORT_DIPNAME( 0x0c, 0x0c, "Bonus Point" )                           PORT_DIPLOCATION("SW2:3,4") /* Extra Life */
	PORT_DIPSETTING(    0x0c, "3000" )
	PORT_DIPSETTING(    0x08, "5000" )
	PORT_DIPSETTING(    0x04, "7000" )
	PORT_DIPSETTING(    0x00, DEF_STR( None ) )
	PORT_DIPNAME( 0x10, 0x10, "Unused" )                                PORT_DIPLOCATION("SW2:5")
	PORT_DIPSETTING(    0x10, DEF_STR( Off ) )
	PORT_DIPSETTING(    0x00, DEF_STR( On ) )
	/* Switches 5 to 8 are shown as completly blank */
INPUT_PORTS_END

static INPUT_PORTS_START( cmanht0a )
	PORT_INCLUDE( decocass )

	PORT_MODIFY("DSW2")
	PORT_DIPNAME( 0x03, 0x03, "Number of Child" )                       PORT_DIPLOCATION("SW2:1,2") /* Lives */
	PORT_DIPSETTING(    0x03, "3" )
	PORT_DIPSETTING(    0x02, "4" )
	PORT_DIPSETTING(    0x01, "5" )
	PORT_DIPSETTING(    0x00, "6" )
	PORT_DIPNAME( 0x0c, 0x0c, "Bonus Point" )                           PORT_DIPLOCATION("SW2:3,4") /* Extra Life */
	PORT_DIPSETTING(    0x0c, "3000" )
	PORT_DIPSETTING(    0x08, "5000" )
	PORT_DIPSETTING(    0x04, "7000" )
	PORT_DIPSETTING(    0x00, DEF_STR( None ) )
	PORT_DIPNAME( 0x10, 0x10, "Unused" )                                PORT_DIPLOCATION("SW2:5")
	PORT_DIPSETTING(    0x10, DEF_STR( Off ) )
	PORT_DIPSETTING(    0x00, DEF_STR( On ) )
	/* Switches 5 to 8 are shown as completly blank */
INPUT_PORTS_END

static INPUT_PORTS_START( cterra2a )
	PORT_INCLUDE( decocass )

	PORT_MODIFY("DSW2")
	PORT_DIPNAME( 0x01, 0x01, "Number of Rockets" )                     PORT_DIPLOCATION("SW2:1") /* Lives */
	PORT_DIPSETTING(    0x01, "3" )
	PORT_DIPSETTING(    0x00, "5" )
	PORT_DIPNAME( 0x06, 0x06, "Bonus Points" )                          PORT_DIPLOCATION("SW2:2,3") /* Extra Life */
	PORT_DIPSETTING(    0x06, "3000" )
	PORT_DIPSETTING(    0x04, "5000" )
	PORT_DIPSETTING(    0x02, "7000" )
	PORT_DIPSETTING(    0x00, DEF_STR( None )  )
	PORT_DIPNAME( 0x08, 0x08, "Player's Rocket Movement" )              PORT_DIPLOCATION("SW2:4")
	PORT_DIPSETTING(    0x08, DEF_STR( Easy ) )
	PORT_DIPSETTING(    0x00, DEF_STR( Difficult ) )
	PORT_DIPNAME( 0x10, 0x10, "Alien Craft Movement" )                  PORT_DIPLOCATION("SW2:5")
	PORT_DIPSETTING(    0x10, DEF_STR( Easy ) )
	PORT_DIPSETTING(    0x00, DEF_STR( Difficult ) )
	/* Switches 6 to 8 are shown as completly blank */
INPUT_PORTS_END

static INPUT_PORTS_START( cnebul2a )
	PORT_INCLUDE( decocass )

	PORT_MODIFY("DSW2")
	PORT_DIPNAME( 0x03, 0x03, "Number of Rockets" )                     PORT_DIPLOCATION("SW2:1,2") /* Lives */
	PORT_DIPSETTING(    0x03, "3" )
	PORT_DIPSETTING(    0x02, "4" )
	PORT_DIPSETTING(    0x01, "5" )
	PORT_DIPSETTING(    0x00, "6" )
	PORT_DIPNAME( 0x0c, 0x0c, "Bonus Point" )                           PORT_DIPLOCATION("SW2:3,4") /* Extra Life */
	PORT_DIPSETTING(    0x0c, "3000" )
	PORT_DIPSETTING(    0x08, "5000" )
	PORT_DIPSETTING(    0x04, "7000" )
	PORT_DIPSETTING(    0x00, DEF_STR( None ) )
	PORT_DIPNAME( 0x10, 0x10, "Unused" )                                PORT_DIPLOCATION("SW2:5")
	PORT_DIPSETTING(    0x10, DEF_STR( Off ) )
	PORT_DIPSETTING(    0x00, DEF_STR( On ) )
	/* Switches 5 to 8 are shown as completly blank */
INPUT_PORTS_END

static INPUT_PORTS_START( castro6a )
	PORT_INCLUDE( decocass )

	PORT_MODIFY("DSW2")
	PORT_DIPNAME( 0x01, 0x01, "Number of Rockets" )                     PORT_DIPLOCATION("SW2:1") /* Lives */
	PORT_DIPSETTING(    0x01, "3" )
	PORT_DIPSETTING(    0x00, "5" )
	PORT_DIPNAME( 0x06, 0x06, "Bonus Points" )                          PORT_DIPLOCATION("SW2:2,3") /* Extra Life */
	PORT_DIPSETTING(    0x06, "3000" )
	PORT_DIPSETTING(    0x04, "5000" )
	PORT_DIPSETTING(    0x02, "7000" )
	PORT_DIPSETTING(    0x00, DEF_STR( None ) )
	PORT_DIPNAME( 0x08, 0x08, "Number of Missiles" )                    PORT_DIPLOCATION("SW2:4")
	PORT_DIPSETTING(    0x08, DEF_STR( Easy ) )
	PORT_DIPSETTING(    0x00, DEF_STR( Difficult ) )
	PORT_DIPNAME( 0x10, 0x10, "Alien Craft Movement" )                  PORT_DIPLOCATION("SW2:5")
	PORT_DIPSETTING(    0x10, DEF_STR( Easy ) )
	PORT_DIPSETTING(    0x00, DEF_STR( Difficult ) )
	/* Switches 6 to 8 are shown as completly blank */
INPUT_PORTS_END

static INPUT_PORTS_START( ctower0a )
	PORT_INCLUDE( decocass )

	PORT_MODIFY("DSW2")
	PORT_DIPNAME( 0x01, 0x01, DEF_STR( Lives ) )                        PORT_DIPLOCATION("SW2:1")
	PORT_DIPSETTING(    0x01, "3" )
	PORT_DIPSETTING(    0x00, "5" )
	PORT_DIPNAME( 0x06, 0x06, DEF_STR( Bonus_Life ) )                   PORT_DIPLOCATION("SW2:2,3")
	PORT_DIPSETTING(    0x06, "20000" )
	PORT_DIPSETTING(    0x04, "30000" )
	PORT_DIPSETTING(    0x02, "40000" )
	PORT_DIPSETTING(    0x00, DEF_STR( None ) )
	PORT_DIPNAME( 0x08, 0x08, DEF_STR( Difficulty ) )                          PORT_DIPLOCATION("SW2:4")
	PORT_DIPSETTING(    0x08, DEF_STR( Easy ) )
	PORT_DIPSETTING(    0x00, DEF_STR( Difficult ) )
	PORT_DIPNAME( 0x10, 0x10, "Unknown" )                               PORT_DIPLOCATION("SW2:5") /* TBD */
	PORT_DIPSETTING(    0x10, DEF_STR( Off ) )
	PORT_DIPSETTING(    0x00, DEF_STR( On ) )
	/* Switches 6 to 8 are shown as completly blank */
INPUT_PORTS_END

static INPUT_PORTS_START( csastf4a )
	PORT_INCLUDE( decocass )

	PORT_MODIFY("DSW2")
	PORT_DIPNAME( 0x01, 0x01, "Number of Rockets" )                     PORT_DIPLOCATION("SW2:1") /* Lives */
	PORT_DIPSETTING(    0x01, "3" )
	PORT_DIPSETTING(    0x00, "5" )
	PORT_DIPNAME( 0x06, 0x06, "Bonus Points" )                          PORT_DIPLOCATION("SW2:2,3") /* Extra Life */
	PORT_DIPSETTING(    0x06, "5000" )
	PORT_DIPSETTING(    0x04, "5000" )
	PORT_DIPSETTING(    0x02, "10000" )
	PORT_DIPSETTING(    0x00, DEF_STR( None )  )
	PORT_DIPNAME( 0x08, 0x08, "Alien Craft Movement" )                  PORT_DIPLOCATION("SW2:4")
	PORT_DIPSETTING(    0x08, DEF_STR( Easy ) )
	PORT_DIPSETTING(    0x00, DEF_STR( Difficult ) )
	PORT_DIPNAME( 0x10, 0x10, "Unused" )                                PORT_DIPLOCATION("SW2:5")
	PORT_DIPSETTING(    0x10, DEF_STR( Off ) )
	PORT_DIPSETTING(    0x00, DEF_STR( On ) )
	/* Switches 5 is left blank, switches 6 to 8 are listed as "Country Code" A through F and "Don't Change" */
INPUT_PORTS_END

static INPUT_PORTS_START( clocknch )
	PORT_INCLUDE( decocass )

	PORT_MODIFY("DSW2")
	PORT_DIPNAME( 0x01, 0x01, DEF_STR( Lives ) )                        PORT_DIPLOCATION("SW2:1")
	PORT_DIPSETTING(    0x01, "3" )
	PORT_DIPSETTING(    0x00, "5" )
	PORT_DIPNAME( 0x06, 0x06, DEF_STR( Bonus_Life ) )                   PORT_DIPLOCATION("SW2:2,3")
	PORT_DIPSETTING(    0x00, DEF_STR( None )  )
	PORT_DIPSETTING(    0x06, "15000" )
	PORT_DIPSETTING(    0x04, "20000" )
	PORT_DIPSETTING(    0x02, "30000" )
	/* Switches 4, 5, 6, 7 & 8 are listed as "Not Used" and "Don't Change" */
INPUT_PORTS_END

static INPUT_PORTS_START( cpgolf1a )
	PORT_INCLUDE( decocass )

	PORT_MODIFY("DSW2")
	PORT_DIPNAME( 0x01, 0x01, "Number of Golfer" )                      PORT_DIPLOCATION("SW2:1") /* Lives */
	PORT_DIPSETTING(    0x01, "2" )
	PORT_DIPSETTING(    0x00, "3" )
	PORT_DIPNAME( 0x06, 0x06, "Bonus Points" )                          PORT_DIPLOCATION("SW2:2,3") /* Extra Life */
	PORT_DIPSETTING(    0x00, DEF_STR( None ) )
	PORT_DIPSETTING(    0x06, "10000" )
	PORT_DIPSETTING(    0x04, "20000" )
	PORT_DIPSETTING(    0x02, "30000" )
	PORT_DIPNAME( 0x08, 0x08, "Number of Shot" )                        PORT_DIPLOCATION("SW2:4")
	PORT_DIPSETTING(    0x08, "Many" )
	PORT_DIPSETTING(    0x00, "Few" )
	PORT_DIPNAME( 0x10, 0x10, "Stroke Power/Ball Direction" )           PORT_DIPLOCATION("SW2:5")
	PORT_DIPSETTING(    0x10, "Indicated" )
	PORT_DIPSETTING(    0x00, "Not Indicated" )
	/* Switches 6, 7 & 8 are listed as "Country Code" A through F and "Don't Change" */
INPUT_PORTS_END

static INPUT_PORTS_START( cpgolf4a )
	PORT_INCLUDE( decocass )

	PORT_MODIFY("DSW2")
	PORT_DIPNAME( 0x01, 0x01, "Number of Golfer" )                      PORT_DIPLOCATION("SW2:1") /* Lives */
	PORT_DIPSETTING(    0x01, "2" )
	PORT_DIPSETTING(    0x00, "3" )
	PORT_DIPNAME( 0x06, 0x06, "Bonus Points" )                          PORT_DIPLOCATION("SW2:2,3") /* Extra Life */
	PORT_DIPSETTING(    0x00, DEF_STR( None ) )
	PORT_DIPSETTING(    0x06, "10000" )
	PORT_DIPSETTING(    0x04, "20000" )
	PORT_DIPSETTING(    0x02, "50000" )
	PORT_DIPNAME( 0x08, 0x08, "Number of Shot" )                        PORT_DIPLOCATION("SW2:4")
	PORT_DIPSETTING(    0x08, "Many" )
	PORT_DIPSETTING(    0x00, "Few" )
	PORT_DIPNAME( 0x10, 0x10, "Stroke Power" )                          PORT_DIPLOCATION("SW2:5")
	PORT_DIPSETTING(    0x10, "Indicated" )
	PORT_DIPSETTING(    0x00, "Not Indicated" )
	/* Switches 6, 7 & 8 are listed as "Country Code" A through F and "Don't Change" */
INPUT_PORTS_END

static INPUT_PORTS_START( cpgolf6a )
	PORT_INCLUDE( decocass )

	PORT_MODIFY("DSW2")
	PORT_DIPNAME( 0x01, 0x01, "Number of Golfer" )                      PORT_DIPLOCATION("SW2:1") /* Lives */
	PORT_DIPSETTING(    0x01, "2" )
	PORT_DIPSETTING(    0x00, "3" )
	PORT_DIPNAME( 0x06, 0x06, DEF_STR( Bonus_Life ) )                   PORT_DIPLOCATION("SW2:2,3") /* You must shoot equal to or under the listed value for a bonus */
	PORT_DIPSETTING(    0x00, DEF_STR( None )  )
	PORT_DIPSETTING(    0x02, "6 Under" )
	PORT_DIPSETTING(    0x04, "3 Under" )
	PORT_DIPSETTING(    0x06, "1 Under" )
	PORT_DIPNAME( 0x08, 0x08, "Number of Strokes" )                     PORT_DIPLOCATION("SW2:4") /* You must shoot equal to or under to continue, else you lose a life */
	PORT_DIPSETTING(    0x00, "Par +2" )
	PORT_DIPSETTING(    0x08, "Par +3" )
	PORT_DIPNAME( 0x10, 0x10, "Show Stroke Power/Ball Direction" )      PORT_DIPLOCATION("SW2:5")
	PORT_DIPSETTING(    0x10, "Indicated" )
	PORT_DIPSETTING(    0x00, "Not Indicated" )
	/* Switches 6, 7 & 8 are listed as "Country Code" A through F and "Don't Change" */
INPUT_PORTS_END

static INPUT_PORTS_START( cpgolf9b )
	PORT_INCLUDE( decocass )

	PORT_MODIFY("DSW2")
	PORT_DIPNAME( 0x01, 0x01, "Number of Golfer" )                      PORT_DIPLOCATION("SW2:1") /* Lives */
	PORT_DIPSETTING(    0x01, "2" )
	PORT_DIPSETTING(    0x00, "3" )
	PORT_DIPNAME( 0x06, 0x06, DEF_STR( Bonus_Life ) )                   PORT_DIPLOCATION("SW2:2,3") /* You must shoot equal to or under the listed value for a bonus */
	PORT_DIPSETTING(    0x00, DEF_STR( None )  )
	PORT_DIPSETTING(    0x02, "6 Under" )
	PORT_DIPSETTING(    0x04, "3 Under" )
	PORT_DIPSETTING(    0x06, "1 Under" )
	PORT_DIPNAME( 0x08, 0x08, "Number of Strokes" )                     PORT_DIPLOCATION("SW2:4") /* You must shoot equal to or under to continue, else you lose a life */
	PORT_DIPSETTING(    0x00, "Par +2" )
	PORT_DIPSETTING(    0x08, "Par +3" )
	PORT_DIPNAME( 0x10, 0x10, "Show Stroke Power/Ball Direction" )      PORT_DIPLOCATION("SW2:5") /* Doesn't work in this version, might be a different option */
	PORT_DIPSETTING(    0x10, "Indicated" )
	PORT_DIPSETTING(    0x00, "Not Indicated" )
	/* Switches 6, 7 & 8 are listed as "Country Code" A through F and "Don't Change" */
INPUT_PORTS_END

static INPUT_PORTS_START( cdstj2a0 )
	PORT_INCLUDE( decocass )

	PORT_MODIFY("DSW2")
	PORT_DIPNAME( 0x03, 0x03, "Time Speed" )                            PORT_DIPLOCATION("SW2:1,2") /* From A to D, time is decreasing faster  */
	PORT_DIPSETTING(    0x03, "A" )
	PORT_DIPSETTING(    0x02, "B" )
	PORT_DIPSETTING(    0x01, "C" )
	PORT_DIPSETTING(    0x00, "D" )
    /* Switches 3 to 8 aren't listed */

	PORT_START("P1_MP0")
	PORT_BIT( 0xff, IP_ACTIVE_HIGH, IPT_UNUSED )

	PORT_START("P1_MP1")
	PORT_BIT( 0x01, IP_ACTIVE_HIGH, IPT_MAHJONG_A ) PORT_PLAYER(1)
	PORT_BIT( 0x02, IP_ACTIVE_HIGH, IPT_MAHJONG_B ) PORT_PLAYER(1)
	PORT_BIT( 0x04, IP_ACTIVE_HIGH, IPT_MAHJONG_C ) PORT_PLAYER(1)
	PORT_BIT( 0x08, IP_ACTIVE_HIGH, IPT_MAHJONG_D ) PORT_PLAYER(1)
	PORT_BIT( 0x10, IP_ACTIVE_HIGH, IPT_MAHJONG_E ) PORT_PLAYER(1)
	PORT_BIT( 0x20, IP_ACTIVE_HIGH, IPT_MAHJONG_F ) PORT_PLAYER(1)
	PORT_BIT( 0x40, IP_ACTIVE_HIGH, IPT_MAHJONG_G ) PORT_PLAYER(1)
	PORT_BIT( 0x80, IP_ACTIVE_HIGH, IPT_UNUSED )


	PORT_START("P1_MP2")
	PORT_BIT( 0x01, IP_ACTIVE_HIGH, IPT_MAHJONG_H ) PORT_PLAYER(1)
	PORT_BIT( 0x02, IP_ACTIVE_HIGH, IPT_MAHJONG_I ) PORT_PLAYER(1)
	PORT_BIT( 0x04, IP_ACTIVE_HIGH, IPT_MAHJONG_J ) PORT_PLAYER(1)
	PORT_BIT( 0x08, IP_ACTIVE_HIGH, IPT_MAHJONG_K ) PORT_PLAYER(1)
	PORT_BIT( 0x10, IP_ACTIVE_HIGH, IPT_MAHJONG_L ) PORT_PLAYER(1)
	PORT_BIT( 0x20, IP_ACTIVE_HIGH, IPT_MAHJONG_M ) PORT_PLAYER(1)
	PORT_BIT( 0x40, IP_ACTIVE_HIGH, IPT_MAHJONG_N ) PORT_PLAYER(1)
	PORT_BIT( 0x80, IP_ACTIVE_HIGH, IPT_UNUSED )

	PORT_START("P1_MP3")
	PORT_BIT( 0x01, IP_ACTIVE_HIGH, IPT_MAHJONG_CHI ) PORT_PLAYER(1)
	PORT_BIT( 0x02, IP_ACTIVE_HIGH, IPT_MAHJONG_PON ) PORT_PLAYER(1)
	PORT_BIT( 0x04, IP_ACTIVE_HIGH, IPT_MAHJONG_KAN ) PORT_PLAYER(1)
	PORT_BIT( 0x08, IP_ACTIVE_HIGH, IPT_MAHJONG_REACH ) PORT_PLAYER(1)
	PORT_BIT( 0x10, IP_ACTIVE_HIGH, IPT_MAHJONG_RON ) PORT_PLAYER(1)
	PORT_BIT( 0x20, IP_ACTIVE_HIGH, IPT_UNUSED )
	PORT_BIT( 0x40, IP_ACTIVE_HIGH, IPT_UNUSED )
	PORT_BIT( 0x80, IP_ACTIVE_HIGH, IPT_UNUSED )

	PORT_START("P2_MP0")
	PORT_BIT( 0xff, IP_ACTIVE_HIGH, IPT_UNUSED )

	PORT_START("P2_MP1")
	PORT_BIT( 0x01, IP_ACTIVE_HIGH, IPT_MAHJONG_A ) PORT_PLAYER(2)
	PORT_BIT( 0x02, IP_ACTIVE_HIGH, IPT_MAHJONG_B ) PORT_PLAYER(2)
	PORT_BIT( 0x04, IP_ACTIVE_HIGH, IPT_MAHJONG_C ) PORT_PLAYER(2)
	PORT_BIT( 0x08, IP_ACTIVE_HIGH, IPT_MAHJONG_D ) PORT_PLAYER(2)
	PORT_BIT( 0x10, IP_ACTIVE_HIGH, IPT_MAHJONG_E ) PORT_PLAYER(2)
	PORT_BIT( 0x20, IP_ACTIVE_HIGH, IPT_MAHJONG_F ) PORT_PLAYER(2)
	PORT_BIT( 0x40, IP_ACTIVE_HIGH, IPT_MAHJONG_G ) PORT_PLAYER(2)
	PORT_BIT( 0x80, IP_ACTIVE_HIGH, IPT_UNUSED )

	PORT_START("P2_MP2")
	PORT_BIT( 0x01, IP_ACTIVE_HIGH, IPT_MAHJONG_H ) PORT_PLAYER(2)
	PORT_BIT( 0x02, IP_ACTIVE_HIGH, IPT_MAHJONG_I ) PORT_PLAYER(2)
	PORT_BIT( 0x04, IP_ACTIVE_HIGH, IPT_MAHJONG_J ) PORT_PLAYER(2)
	PORT_BIT( 0x08, IP_ACTIVE_HIGH, IPT_MAHJONG_K ) PORT_PLAYER(2)
	PORT_BIT( 0x10, IP_ACTIVE_HIGH, IPT_MAHJONG_L ) PORT_PLAYER(2)
	PORT_BIT( 0x20, IP_ACTIVE_HIGH, IPT_MAHJONG_M ) PORT_PLAYER(2)
	PORT_BIT( 0x40, IP_ACTIVE_HIGH, IPT_MAHJONG_N ) PORT_PLAYER(2)
	PORT_BIT( 0x80, IP_ACTIVE_HIGH, IPT_UNUSED )

	PORT_START("P2_MP3")
	PORT_BIT( 0x01, IP_ACTIVE_HIGH, IPT_MAHJONG_CHI ) PORT_PLAYER(2)
	PORT_BIT( 0x02, IP_ACTIVE_HIGH, IPT_MAHJONG_PON ) PORT_PLAYER(2)
	PORT_BIT( 0x04, IP_ACTIVE_HIGH, IPT_MAHJONG_KAN ) PORT_PLAYER(2)
	PORT_BIT( 0x08, IP_ACTIVE_HIGH, IPT_MAHJONG_REACH ) PORT_PLAYER(2)
	PORT_BIT( 0x10, IP_ACTIVE_HIGH, IPT_MAHJONG_RON ) PORT_PLAYER(2)
	PORT_BIT( 0x20, IP_ACTIVE_HIGH, IPT_UNUSED )
	PORT_BIT( 0x40, IP_ACTIVE_HIGH, IPT_UNUSED )
	PORT_BIT( 0x80, IP_ACTIVE_HIGH, IPT_UNUSED )
INPUT_PORTS_END

static INPUT_PORTS_START( cexplr0a )
	PORT_INCLUDE( decocass )

	PORT_MODIFY("DSW2")
	PORT_DIPNAME( 0x01, 0x01, "Number of Space Ship" )                  PORT_DIPLOCATION("SW2:1") /* Lives */
	PORT_DIPSETTING(    0x01, "3" )
	PORT_DIPSETTING(    0x00, "5" )
	PORT_DIPNAME( 0x06, 0x06, "Bonus Points" )                          PORT_DIPLOCATION("SW2:2,3") /* Extra Life */
	PORT_DIPSETTING(    0x06, "10000" )
	PORT_DIPSETTING(    0x04, "1500000" )
	PORT_DIPSETTING(    0x02, "30000" )
	PORT_DIPSETTING(    0x00, DEF_STR( None ) )
	PORT_DIPNAME( 0x08, 0x08, "Missile" )                               PORT_DIPLOCATION("SW2:4")
	PORT_DIPSETTING(    0x08, DEF_STR( Easy ) )
	PORT_DIPSETTING(    0x00, DEF_STR( Difficult ) )
	PORT_DIPNAME( 0x10, 0x10, "Number of UFO" )                         PORT_DIPLOCATION("SW2:5")
	PORT_DIPSETTING(    0x10, "Few" )
	PORT_DIPSETTING(    0x00, "Many" )
	/* Switches 6, 7 & 8 are listed as "Don't Change" */
INPUT_PORTS_END

static INPUT_PORTS_START( ctornado )
	PORT_INCLUDE( decocass )

	PORT_MODIFY("DSW2")
	PORT_DIPNAME( 0x01, 0x01, DEF_STR( Lives ) )                        PORT_DIPLOCATION("SW2:1")
	PORT_DIPSETTING(    0x01, "3" )
	PORT_DIPSETTING(    0x00, "5" )
	PORT_DIPNAME( 0x06, 0x06, DEF_STR( Bonus_Life ) )                   PORT_DIPLOCATION("SW2:2,3")
	PORT_DIPSETTING(    0x00, DEF_STR( None ) )
	PORT_DIPSETTING(    0x06, "10000" )
	PORT_DIPSETTING(    0x04, "20000" )
	PORT_DIPSETTING(    0x02, "30000" )
	PORT_DIPNAME( 0x08, 0x08, "Crash Bombs" )                           PORT_DIPLOCATION("SW2:4") /* Printed English translation "Hero Destructor" */
	PORT_DIPSETTING(    0x08, "3" )
	PORT_DIPSETTING(    0x00, "5" )
	PORT_DIPNAME( 0x10, 0x10, "Aliens' Speed" )                          PORT_DIPLOCATION("SW2:5")
	PORT_DIPSETTING(    0x10, "Slow" )
	PORT_DIPSETTING(    0x00, "Fast" )
	/* Switches 6, 7 & 8 are listed as "Country Code" A through F and "Don't Change" */
INPUT_PORTS_END

static INPUT_PORTS_START( cmissnx )
	PORT_INCLUDE( decocass )

	PORT_MODIFY("DSW2")
	PORT_DIPNAME( 0x01, 0x01, DEF_STR( Lives ) )                        PORT_DIPLOCATION("SW2:1")
	PORT_DIPSETTING(    0x01, "3" )
	PORT_DIPSETTING(    0x00, "5" )
	PORT_DIPNAME( 0x06, 0x06, DEF_STR( Bonus_Life ) )                   PORT_DIPLOCATION("SW2:2,3")
	PORT_DIPSETTING(    0x00, DEF_STR( None )  )
	PORT_DIPSETTING(    0x06, "5000" )
	PORT_DIPSETTING(    0x04, "10000" )
	PORT_DIPSETTING(    0x02, "15000" )
	PORT_DIPNAME( 0x18, 0x18, DEF_STR( Difficulty ) )                   PORT_DIPLOCATION("SW2:4,5") /* Listed as "Game Level" */
	PORT_DIPSETTING(    0x18, DEF_STR( Easy ) )
	PORT_DIPSETTING(    0x10, DEF_STR( Normal ) )
	PORT_DIPSETTING(    0x08, DEF_STR( Hard ) )
	PORT_DIPSETTING(    0x00, DEF_STR( Hardest ) )
	/* Switches 6, 7 & 8 are listed as "Country Code" A through F and "Don't Change" */
INPUT_PORTS_END

static INPUT_PORTS_START( cbtime )
	PORT_INCLUDE( decocass )

	PORT_MODIFY("DSW2")
	PORT_DIPNAME( 0x01, 0x01, DEF_STR( Lives ) )                        PORT_DIPLOCATION("SW2:1")
	PORT_DIPSETTING(    0x01, "3" )
	PORT_DIPSETTING(    0x00, "5" )
	PORT_DIPNAME( 0x06, 0x06, DEF_STR( Bonus_Life ) )                   PORT_DIPLOCATION("SW2:2,3")
	PORT_DIPSETTING(    0x06, "20000" )
	PORT_DIPSETTING(    0x04, "30000" )
	PORT_DIPSETTING(    0x02, "40000" )
	PORT_DIPSETTING(    0x00, "50000" )
	PORT_DIPNAME( 0x08, 0x08, "Enemies" )                               PORT_DIPLOCATION("SW2:4")
	PORT_DIPSETTING(    0x08, "4" )
	PORT_DIPSETTING(    0x00, "6" )
	PORT_DIPNAME( 0x10, 0x10, "End of Level Pepper" )                   PORT_DIPLOCATION("SW2:5")
	PORT_DIPSETTING(    0x10, DEF_STR( No ) )
	PORT_DIPSETTING(    0x00, DEF_STR( Yes ) )
INPUT_PORTS_END

static INPUT_PORTS_START( cgraplop )
	PORT_INCLUDE( decocass )

	PORT_MODIFY("DSW2")
	PORT_DIPNAME( 0x01, 0x01, DEF_STR( Lives ) )                        PORT_DIPLOCATION("SW2:1")
	PORT_DIPSETTING(    0x01, "3" )
	PORT_DIPSETTING(    0x00, "5" )
	PORT_DIPNAME( 0x06, 0x06, DEF_STR( Bonus_Life ) )                   PORT_DIPLOCATION("SW2:2,3")
	PORT_DIPSETTING(    0x00, DEF_STR( None )  )
	PORT_DIPSETTING(    0x06, "20000" )
	PORT_DIPSETTING(    0x04, "50000" )
	PORT_DIPSETTING(    0x02, "70000" )
	PORT_DIPNAME( 0x08, 0x08, "Number of Up Sign" )                     PORT_DIPLOCATION("SW2:4")
	PORT_DIPSETTING(    0x08, "Few" )
	PORT_DIPSETTING(    0x00, "Many" )
	PORT_DIPNAME( 0x10, 0x10, "Falling Speed" )                         PORT_DIPLOCATION("SW2:5")
	PORT_DIPSETTING(    0x10, DEF_STR( Easy ) )
	PORT_DIPSETTING(    0x00, DEF_STR( Difficult ) )
	/* Switches 6, 7 & 8 are listed as "Not Used" and "Don't Change" */
INPUT_PORTS_END

static INPUT_PORTS_START( cnightst )
	PORT_INCLUDE( decocass )

	PORT_MODIFY("DSW2")
	PORT_DIPNAME( 0x01, 0x01, DEF_STR( Lives ) )                        PORT_DIPLOCATION("SW2:1")
	PORT_DIPSETTING(    0x01, "3" )
	PORT_DIPSETTING(    0x00, "5" )
	PORT_DIPNAME( 0x06, 0x06, DEF_STR( Bonus_Life ) )                   PORT_DIPLOCATION("SW2:2,3")
	PORT_DIPSETTING(    0x06, "When Night Star Completed (First 2 Times)" )
	PORT_DIPSETTING(    0x04, "When Night Star Completed (First Time Only)" )
	PORT_DIPSETTING(    0x02, "Every 70000"  )
	PORT_DIPSETTING(    0x00, "30000 Only"  )
	PORT_DIPNAME( 0x08, 0x08, "Number of Missles" )                     PORT_DIPLOCATION("SW2:4")
	PORT_DIPSETTING(    0x08, "Few" )
	PORT_DIPSETTING(    0x00, "Many" )
	PORT_DIPNAME( 0x10, 0x10, "Enemy's Speed" )                         PORT_DIPLOCATION("SW2:5")
	PORT_DIPSETTING(    0x10, "Slow" )
	PORT_DIPSETTING(    0x00, "Fast" )
	/* Switches 6, 7 & 8 are listed as "Country Code" A through F and "Don't Change" */
INPUT_PORTS_END

static INPUT_PORTS_START( cskater )
	PORT_INCLUDE( decocass )

	PORT_MODIFY("DSW2")
	PORT_DIPNAME( 0x01, 0x01, DEF_STR( Lives ) )                        PORT_DIPLOCATION("SW2:1") /* Listed as "Number of Balls" */
	PORT_DIPSETTING(    0x01, "3" )
	PORT_DIPSETTING(    0x00, "5" )
	PORT_DIPNAME( 0x06, 0x06, DEF_STR( Bonus_Life ) )                   PORT_DIPLOCATION("SW2:2,3")
	PORT_DIPSETTING(    0x00, "60000" )
	PORT_DIPSETTING(    0x06, "20000" )
	PORT_DIPSETTING(    0x04, "30000" )
	PORT_DIPSETTING(    0x02, "40000" )
	PORT_DIPNAME( 0x08, 0x08, "Enemy's Speed" )                         PORT_DIPLOCATION("SW2:4")
	PORT_DIPSETTING(    0x08, DEF_STR( Easy ) )
	PORT_DIPSETTING(    0x00, DEF_STR( Difficult ) )
	PORT_DIPNAME( 0x10, 0x10, "Number of Skates" )                      PORT_DIPLOCATION("SW2:5")
	PORT_DIPSETTING(    0x10, "Small" )
	PORT_DIPSETTING(    0x00, "Large" )
	/* Switches 6, 7 & 8 are listed as "Country Code" A through F and "Don't Change" */
INPUT_PORTS_END

static INPUT_PORTS_START( cpsoccer )
	PORT_INCLUDE( decocass )

	PORT_MODIFY("DSW2")
	PORT_DIPNAME( 0x01, 0x01, DEF_STR( Lives ) )                        PORT_DIPLOCATION("SW2:1") /* Listed as "Number of Balls" */
	PORT_DIPSETTING(    0x01, "3" )
	PORT_DIPSETTING(    0x00, "5" )
	PORT_DIPNAME( 0x06, 0x06, "Number of Nice Goal" )                   PORT_DIPLOCATION("SW2:2,3")
	PORT_DIPSETTING(    0x00, DEF_STR( None )  )
	PORT_DIPSETTING(    0x06, "5" )
	PORT_DIPSETTING(    0x04, "10" )
	PORT_DIPSETTING(    0x02, "20" )
	PORT_DIPNAME( 0x08, 0x08, DEF_STR( Demo_Sounds ) )                  PORT_DIPLOCATION("SW2:4")
	PORT_DIPSETTING(    0x00, DEF_STR( Off ) )
	PORT_DIPSETTING(    0x08, DEF_STR( On ) )
	PORT_DIPNAME( 0x10, 0x10, DEF_STR( Difficulty ) )                   PORT_DIPLOCATION("SW2:4") /* Listed as "Class" */
	PORT_DIPSETTING(    0x10, DEF_STR( Easy ) )
	PORT_DIPSETTING(    0x00, DEF_STR( Difficult ) )
	/* Switches 6, 7 & 8 are listed as "Country Code" A through F and "Don't Change" */
INPUT_PORTS_END

static INPUT_PORTS_START( csdtenis )
	PORT_INCLUDE( decocass )

	PORT_MODIFY("DSW2")
	PORT_DIPNAME( 0x01, 0x01, DEF_STR( Lives ) )                        PORT_DIPLOCATION("SW2:1") /* Listed as "Number of Balls" */
	PORT_DIPSETTING(    0x01, "2" )
	PORT_DIPSETTING(    0x00, "1" )
	PORT_DIPNAME( 0x06, 0x06, DEF_STR( Bonus_Life ) )                   PORT_DIPLOCATION("SW2:2,3")
	PORT_DIPSETTING(    0x00, DEF_STR( None )  )
	PORT_DIPSETTING(    0x06, "Every 1set" )
	PORT_DIPSETTING(    0x04, "Every 2set" )
	PORT_DIPSETTING(    0x02, "Every 3set" )
	PORT_DIPNAME( 0x08, 0x08, "Speed Level" )                           PORT_DIPLOCATION("SW2:4")
	PORT_DIPSETTING(    0x08, "Low Speed" )
	PORT_DIPSETTING(    0x00, "High Speed" )
	PORT_DIPNAME( 0x10, 0x10, "Attack Level" )                          PORT_DIPLOCATION("SW2:5")
	PORT_DIPSETTING(    0x10, DEF_STR( Easy ) )
	PORT_DIPSETTING(    0x00, DEF_STR( Difficult ) )
	/* Switches 6, 7 & 8 are listed as "Country Code" A through F and "Don't Change" */
INPUT_PORTS_END

static INPUT_PORTS_START( cscrtry )
	PORT_INCLUDE( decocass )

	PORT_MODIFY("DSW2")
	PORT_DIPNAME( 0x01, 0x01, DEF_STR( Lives ) )                        PORT_DIPLOCATION("SW2:1")
	PORT_DIPSETTING(    0x01, "2" )
	PORT_DIPSETTING(    0x00, "4" )
	PORT_DIPNAME( 0x06, 0x06, DEF_STR( Bonus_Life ) )                   PORT_DIPLOCATION("SW2:2,3")
	PORT_DIPSETTING(    0x00, DEF_STR( None )  )
	PORT_DIPSETTING(    0x06, "30000" )
	PORT_DIPSETTING(    0x04, "50000" )
	PORT_DIPSETTING(    0x02, "70000" )
	PORT_DIPNAME( 0x08, 0x08, DEF_STR( Difficulty ) )                   PORT_DIPLOCATION("SW2:4")
	PORT_DIPSETTING(    0x08, DEF_STR( Easy ) )
	PORT_DIPSETTING(    0x00, DEF_STR( Difficult ) )
	PORT_DIPNAME( 0x10, 0x10, "Timer(Don't Change)" )                   PORT_DIPLOCATION("SW2:5")
	PORT_DIPSETTING(    0x10, "Timer decrease" )
	PORT_DIPSETTING(    0x00, "Timer infinity" )
	/* Switches 6, 7 & 8 are listed as "Special Purpose" and "Don't Change" */
INPUT_PORTS_END

static INPUT_PORTS_START( cfghtice )
	PORT_INCLUDE( decocass )

	PORT_MODIFY("DSW2")
	PORT_DIPNAME( 0x01, 0x01, DEF_STR( Lives ) )                        PORT_DIPLOCATION("SW2:1")
	PORT_DIPSETTING(    0x01, "3" )
	PORT_DIPSETTING(    0x00, "5" )
	PORT_DIPNAME( 0x06, 0x06, DEF_STR( Very_Difficult ) )               PORT_DIPLOCATION("SW2:2,3")
	PORT_DIPSETTING(    0x04, DEF_STR( Very_Easy )  )
	PORT_DIPSETTING(    0x06, DEF_STR( Easy ) )
	PORT_DIPSETTING(    0x00, DEF_STR( Difficult ) )
	PORT_DIPSETTING(    0x02, DEF_STR( Very_Difficult ) )
	PORT_DIPNAME( 0x08, 0x08, "Enemy's Speed" )                         PORT_DIPLOCATION("SW2:4")
	PORT_DIPSETTING(    0x08, DEF_STR( Normal ) )
	PORT_DIPSETTING(    0x00, "Fast" )
	PORT_SERVICE_DIPLOC(  0x10, IP_ACTIVE_LOW, "SW2:5" )    /* Listed as Test Mode, but doesn't seem to work??? */
	/* Switches 6, 7 & 8 are listed as "Country Code" A through F and "Don't Change" */
INPUT_PORTS_END

static INPUT_PORTS_START( cbdash )
	PORT_INCLUDE( decocass )

	PORT_MODIFY("DSW2")
	PORT_DIPNAME( 0x01, 0x01, DEF_STR( Lives ) )                        PORT_DIPLOCATION("SW2:1")
	PORT_DIPSETTING(    0x01, "3" )
	PORT_DIPSETTING(    0x00, "5" )
	PORT_DIPNAME( 0x06, 0x06, DEF_STR( Bonus_Life ) )                   PORT_DIPLOCATION("SW2:2,3")
	PORT_DIPSETTING(    0x00, DEF_STR( None )  )
	PORT_DIPSETTING(    0x06, "20000" )
	PORT_DIPSETTING(    0x04, "30000" )
	PORT_DIPSETTING(    0x02, "40000" )
	PORT_DIPNAME( 0x18, 0x18, DEF_STR( Difficulty ) )                   PORT_DIPLOCATION("SW2:4,5")
	PORT_DIPSETTING(    0x18, DEF_STR( Normal ) )       /* Number of Diamonds Little, Timer: Long */
	PORT_DIPSETTING(    0x10, DEF_STR( Hard ) )     /* Number of Diamonds Little, Timer: Long */
	PORT_DIPSETTING(    0x08, DEF_STR( Harder ) )       /* Number of Diamonds Many, Timer: Short */
	PORT_DIPSETTING(    0x00, DEF_STR( Hardest ) )      /* Number of Diamonds Many, Timer: Short */
	/* Switches 6, 7 & 8 are listed as "Country Code" A through F and "Don't Change" */
INPUT_PORTS_END

static INPUT_PORTS_START( cfishing )
	PORT_INCLUDE( decocass )

	PORT_MODIFY("DSW2")
	PORT_DIPNAME( 0x01, 0x01, DEF_STR( Lives ) )                        PORT_DIPLOCATION("SW2:1")
	PORT_DIPSETTING(    0x01, "3" )
	PORT_DIPSETTING(    0x00, "5" )
	PORT_DIPNAME( 0x06, 0x06, DEF_STR( Bonus_Life ) )                   PORT_DIPLOCATION("SW2:2,3")
	PORT_DIPSETTING(    0x00, DEF_STR( None )  )
	PORT_DIPSETTING(    0x06, "10000" )
	PORT_DIPSETTING(    0x04, "20000" )
	PORT_DIPSETTING(    0x02, "30000" )
	PORT_DIPNAME( 0x08, 0x08, DEF_STR( Difficulty ) )                   PORT_DIPLOCATION("SW2:4")
	PORT_DIPSETTING(    0x08, DEF_STR( Easy ) )
	PORT_DIPSETTING(    0x00, DEF_STR( Difficult ) )
	/* Switches 5, 6, 7 & 8 are listed as "Not Used" and "Don't Change" */
INPUT_PORTS_END


static const gfx_layout charlayout =
{
	8,8,
	1024,
	3,
	{ 2*1024*8*8, 1*1024*8*8, 0*1024*8*8 },
	{ STEP8(0,1) },
	{ STEP8(0,8) },
	8*8
};

static const gfx_layout spritelayout =
{
	16,16,
	256,
	3,
	{ 2*256*16*16, 1*256*16*16, 0*256*16*16 },
	{ STEP8(16*8,1), STEP8(0*8,1) },
	{ STEP16(0,8) },
	32*8
};

static const gfx_layout tilelayout =
{
	16,16,
	16,
	3,
	{ 2*16*16*16+4, 2*16*16*16+0, 4 },
	{ STEP4(3*16*8,1), STEP4(2*16*8,1), STEP4(1*16*8,1), STEP4(0*16*8,1) },
	{ STEP16(0,8) },
	2*16*16
};

static const UINT32 objlayout_xoffset[64] =
{
	STEP8(7*8,1), STEP8(6*8,1), STEP8(5*8,1), STEP8(4*8,1),
	STEP8(3*8,1), STEP8(2*8,1), STEP8(1*8,1), STEP8(0*8,1)
};

static const UINT32 objlayout_yoffset[64] =
{
	STEP32(63*2*64, -1*2*64),
	STEP32(31*2*64, -1*2*64)
};

static const gfx_layout objlayout =
{
	64,64,  /* 64x64 object */
	2,      /* 2 objects */
	1,      /* 1 bits per pixel */
	{ 0 },
	EXTENDED_XOFFS,
	EXTENDED_YOFFS,
	8*8, /* object takes 8 consecutive bytes */
	objlayout_xoffset,
	objlayout_yoffset
};

static GFXDECODE_START( decocass )
	GFXDECODE_ENTRY( nullptr, 0x6000, charlayout,       0, 4 )  /* char set #1 */
	GFXDECODE_ENTRY( nullptr, 0x6000, spritelayout,     0, 4 )  /* sprites */
	GFXDECODE_ENTRY( nullptr, 0xd000, tilelayout,      32, 2 )  /* background tiles */
	GFXDECODE_ENTRY( nullptr, 0xd800, objlayout,       48, 4 )  /* object */
GFXDECODE_END

PALETTE_INIT_MEMBER(decocass_state, decocass)
{
	int i;

	/* set up 32 colors 1:1 pens */
	for (i = 0; i < 32; i++)
		palette.set_pen_indirect(i, i);

	/* setup straight/flipped colors for background tiles (D7 of color_center_bot ?) */
	for (i = 0; i < 8; i++)
	{
		palette.set_pen_indirect(32+i, 3*8+i);
		palette.set_pen_indirect(40+i, 3*8+((i << 1) & 0x04) + ((i >> 1) & 0x02) + (i & 0x01));
	}

	/* setup 4 colors for 1bpp object */
	palette.set_pen_indirect(48+0*2+0, 0);
	palette.set_pen_indirect(48+0*2+1, 25); /* testtape red from 4th palette section? */
	palette.set_pen_indirect(48+1*2+0, 0);
	palette.set_pen_indirect(48+1*2+1, 28); /* testtape blue from 4th palette section? */
	palette.set_pen_indirect(48+2*2+0, 0);
	palette.set_pen_indirect(48+2*2+1, 26); /* testtape green from 4th palette section? */
	palette.set_pen_indirect(48+3*2+0, 0);
	palette.set_pen_indirect(48+3*2+1, 23); /* ???? */
}


static MACHINE_CONFIG_START( decocass, decocass_state )

	/* basic machine hardware */
	MCFG_CPU_ADD("maincpu", DECO_222, HCLK4) /* the earlier revision board doesn't have the 222 but must have the same thing implemented in logic for the M6502 */
	MCFG_CPU_PROGRAM_MAP(decocass_map)

	MCFG_CPU_ADD("audiocpu", M6502, HCLK1/3/2)
	MCFG_CPU_PROGRAM_MAP(decocass_sound_map)
	MCFG_TIMER_DRIVER_ADD_SCANLINE("audionmi", decocass_state, decocass_audio_nmi_gen, "screen", 0, 8)

	MCFG_CPU_ADD("mcu", I8041, HCLK)
	MCFG_CPU_IO_MAP(decocass_mcu_portmap)

	MCFG_QUANTUM_TIME(attotime::from_hz(4200))              /* interleave CPUs */


	MCFG_DECOCASS_TAPE_ADD("cassette")

	/* video hardware */
	MCFG_SCREEN_ADD("screen", RASTER)
	MCFG_SCREEN_RAW_PARAMS(HCLK, 384, 0*8, 256, 272, 1*8, 248)
	MCFG_SCREEN_UPDATE_DRIVER(decocass_state, screen_update_decocass)
	MCFG_SCREEN_PALETTE("palette")

	MCFG_GFXDECODE_ADD("gfxdecode", "palette", decocass)
	MCFG_PALETTE_ADD("palette", 32+2*8+2*4)
	MCFG_PALETTE_INDIRECT_ENTRIES(32)
	MCFG_PALETTE_INIT_OWNER(decocass_state, decocass)

	/* sound hardware */
	MCFG_SPEAKER_STANDARD_MONO("mono")

	MCFG_SOUND_ADD("ay1", AY8910, HCLK2)
	MCFG_SOUND_ROUTE(ALL_OUTPUTS, "mono", 0.40)

	MCFG_SOUND_ADD("ay2", AY8910, HCLK2)
	MCFG_SOUND_ROUTE(ALL_OUTPUTS, "mono", 0.40)
MACHINE_CONFIG_END


static MACHINE_CONFIG_DERIVED( ctsttape, decocass )

	/* basic machine hardware */
	MCFG_MACHINE_RESET_OVERRIDE(decocass_state,ctsttape)
MACHINE_CONFIG_END


static MACHINE_CONFIG_DERIVED( chwych0a, decocass )

	/* basic machine hardware */
	MCFG_MACHINE_RESET_OVERRIDE(decocass_state,chwych0a)
MACHINE_CONFIG_END


static MACHINE_CONFIG_DERIVED( cninjt0a, decocass )

	/* basic machine hardware */
	MCFG_MACHINE_RESET_OVERRIDE(decocass_state,cninjt0a)
MACHINE_CONFIG_END


static MACHINE_CONFIG_DERIVED( cmanht0a, decocass )

	/* basic machine hardware */
	MCFG_MACHINE_RESET_OVERRIDE(decocass_state,cmanht0a)
MACHINE_CONFIG_END


static MACHINE_CONFIG_DERIVED( cterra2a, decocass )

	/* basic machine hardware */
	MCFG_MACHINE_RESET_OVERRIDE(decocass_state,cterra2a)
MACHINE_CONFIG_END


static MACHINE_CONFIG_DERIVED( cnebul2a, decocass )

	/* basic machine hardware */
	MCFG_MACHINE_RESET_OVERRIDE(decocass_state,cnebul2a)
MACHINE_CONFIG_END


static MACHINE_CONFIG_DERIVED( castro6a, decocass )

	/* basic machine hardware */
	MCFG_MACHINE_RESET_OVERRIDE(decocass_state,castro6a)
MACHINE_CONFIG_END


static MACHINE_CONFIG_DERIVED( ctower0a, decocass )

	/* basic machine hardware */
	MCFG_MACHINE_RESET_OVERRIDE(decocass_state,ctower0a)
MACHINE_CONFIG_END


static MACHINE_CONFIG_DERIVED( csastf4a, decocass )

	/* basic machine hardware */
	MCFG_MACHINE_RESET_OVERRIDE(decocass_state,csastf4a)
MACHINE_CONFIG_END


static MACHINE_CONFIG_DERIVED( clocknch, decocass )

	/* basic machine hardware */
	MCFG_MACHINE_RESET_OVERRIDE(decocass_state,clocknch)
MACHINE_CONFIG_END


static MACHINE_CONFIG_DERIVED( cpgolf1a, decocass )

	/* basic machine hardware */
	MCFG_MACHINE_RESET_OVERRIDE(decocass_state,cpgolf1a)
MACHINE_CONFIG_END


static MACHINE_CONFIG_DERIVED( cdstj2a0, decocass )

	/* basic machine hardware */
	MCFG_MACHINE_RESET_OVERRIDE(decocass_state,cdstj2a0)
MACHINE_CONFIG_END


static MACHINE_CONFIG_DERIVED( cfishing, decocass )

	/* basic machine hardware */
	MCFG_MACHINE_RESET_OVERRIDE(decocass_state,cfishing)
MACHINE_CONFIG_END


static MACHINE_CONFIG_DERIVED( cluckypo, decocass )

	/* basic machine hardware */
	MCFG_MACHINE_RESET_OVERRIDE(decocass_state,cluckypo)
MACHINE_CONFIG_END


static MACHINE_CONFIG_DERIVED( ctisland, decocass )

	/* basic machine hardware */
	MCFG_MACHINE_RESET_OVERRIDE(decocass_state,ctisland)
MACHINE_CONFIG_END


static MACHINE_CONFIG_DERIVED( cexplr0a, decocass )

	/* basic machine hardware */
	MCFG_MACHINE_RESET_OVERRIDE(decocass_state,cexplr0a)
MACHINE_CONFIG_END


static MACHINE_CONFIG_DERIVED( cdiscon1, decocass )

	/* basic machine hardware */
	MCFG_MACHINE_RESET_OVERRIDE(decocass_state,cdiscon1)
MACHINE_CONFIG_END


static MACHINE_CONFIG_DERIVED( ctornado, decocass )

	/* basic machine hardware */
	MCFG_MACHINE_RESET_OVERRIDE(decocass_state,ctornado)
MACHINE_CONFIG_END


static MACHINE_CONFIG_DERIVED( cmissnx, decocass )

	/* basic machine hardware */
	MCFG_MACHINE_RESET_OVERRIDE(decocass_state,cmissnx)
MACHINE_CONFIG_END


static MACHINE_CONFIG_DERIVED( cptennis, decocass )

	/* basic machine hardware */
	MCFG_MACHINE_RESET_OVERRIDE(decocass_state,cptennis)
MACHINE_CONFIG_END


static MACHINE_CONFIG_DERIVED( cbtime, decocass )

	/* basic machine hardware */
	MCFG_MACHINE_RESET_OVERRIDE(decocass_state,cbtime)
MACHINE_CONFIG_END


static MACHINE_CONFIG_DERIVED( cburnrub, decocass )

	/* basic machine hardware */
	MCFG_MACHINE_RESET_OVERRIDE(decocass_state,cburnrub)
MACHINE_CONFIG_END


static MACHINE_CONFIG_DERIVED( cgraplop, decocass )

	/* basic machine hardware */
	MCFG_MACHINE_RESET_OVERRIDE(decocass_state,cgraplop)
MACHINE_CONFIG_END


static MACHINE_CONFIG_DERIVED( cgraplop2, decocass )

	/* basic machine hardware */
	MCFG_MACHINE_RESET_OVERRIDE(decocass_state,cgraplop2)
MACHINE_CONFIG_END


static MACHINE_CONFIG_DERIVED( clapapa, decocass )

	/* basic machine hardware */
	MCFG_MACHINE_RESET_OVERRIDE(decocass_state,clapapa)
MACHINE_CONFIG_END


static MACHINE_CONFIG_DERIVED( cskater, decocass )

	/* basic machine hardware */
	MCFG_MACHINE_RESET_OVERRIDE(decocass_state,cskater)
MACHINE_CONFIG_END


static MACHINE_CONFIG_DERIVED( cprobowl, decocass )

	/* basic machine hardware */
	MCFG_MACHINE_RESET_OVERRIDE(decocass_state,cprobowl)
MACHINE_CONFIG_END


static MACHINE_CONFIG_DERIVED( cnightst, decocass )

	/* basic machine hardware */
	MCFG_MACHINE_RESET_OVERRIDE(decocass_state,cnightst)
MACHINE_CONFIG_END


static MACHINE_CONFIG_DERIVED( cpsoccer, decocass )

	/* basic machine hardware */
	MCFG_MACHINE_RESET_OVERRIDE(decocass_state,cpsoccer)
MACHINE_CONFIG_END


static MACHINE_CONFIG_DERIVED( csdtenis, decocass )

	/* basic machine hardware */
	MCFG_MACHINE_RESET_OVERRIDE(decocass_state,csdtenis)
MACHINE_CONFIG_END


static MACHINE_CONFIG_DERIVED( czeroize, decocass )

	/* basic machine hardware */
	MCFG_MACHINE_RESET_OVERRIDE(decocass_state,czeroize)
MACHINE_CONFIG_END


static MACHINE_CONFIG_DERIVED( cppicf, decocass )

	/* basic machine hardware */
	MCFG_MACHINE_RESET_OVERRIDE(decocass_state,cppicf)
MACHINE_CONFIG_END


static MACHINE_CONFIG_DERIVED( cfghtice, decocass )

	/* basic machine hardware */
	MCFG_MACHINE_RESET_OVERRIDE(decocass_state,cfghtice)
MACHINE_CONFIG_END


static MACHINE_CONFIG_DERIVED( type4, decocass )

	/* basic machine hardware */
	MCFG_MACHINE_RESET_OVERRIDE(decocass_state,type4)
MACHINE_CONFIG_END


static MACHINE_CONFIG_DERIVED( cbdash, decocass )

	/* basic machine hardware */
	MCFG_MACHINE_RESET_OVERRIDE(decocass_state,cbdash)
MACHINE_CONFIG_END


static MACHINE_CONFIG_DERIVED( cflyball, decocass )

	/* basic machine hardware */
	MCFG_MACHINE_RESET_OVERRIDE(decocass_state,cflyball)
MACHINE_CONFIG_END


#define DECOCASS_COMMON_ROMS    \
	/* basic roms for new system */ \
	ROM_REGION( 0x10000, "audiocpu", 0 )      \
	ROM_LOAD( "v1.5a",      0xf800, 0x0800, CRC(b66b2c2a) SHA1(0097f38beb4872e735e560148052e258a26b08fd) ) /* from RMS-8 board: 2716 eprom @5A w/V1- label,  contains audio cpu code */ \
\
	ROM_REGION( 0x10000, "mcu", 0 )   /* 4k for the 8041 MCU (actually 1K ROM + 64 bytes RAM @ 0x800) */ \
	ROM_LOAD( "cassmcu.1c", 0x0000, 0x0400, CRC(a6df18fd) SHA1(1f9ea47e372d31767c936c15852b43df2b0ee8ff) ) /* from B10-B board: "NEC // JAPAN // X1202D-108 // D8041C 535" 8041 MCU @1C, handles cassette and other stuff; This info needs additional verification, as the d8041-535 mcu has not been dumped yet to prove code is the same. */ \
\
	ROM_REGION( 0x00060, "proms", 0 )     /* PROMS */ \
	ROM_LOAD( "v2.3m",      0x0000, 0x0020, CRC(238fdb40) SHA1(b88e8fabb82092105c3828154608ea067acbf2e5) ) /* from DSP-8 board: M3-7603-5 (82s123 equiv, 32x8 TS) PROM @3M w/'V2' stamp, unknown purpose (gfx related: row/interrupt/vblank related? vertical counter related) */ \
	ROM_LOAD( "v4.10d",     0x0020, 0x0020, CRC(3b5836b4) SHA1(b630bb277d9ec09d46ef26b944014dd6165b35d8) ) /* from DSP-8 board: M3-7603-5 (82s123 equiv, 32x8 TS) PROM @10D w/'V4' stamp, unknown purpose (gfx related: tile banking? horizontal counter related) */ \
	ROM_LOAD( "v3.3j",      0x0040, 0x0020, CRC(51eef657) SHA1(eaedce5caf55624ad6ae706aedf82c5717c60f1f) ) /* from RMS-8 board: M3-7603-5 (82s123 equiv, 32x8 TS) PROM @3J w/'V3' stamp, handles DRAM banking and timing */

#define DECOCASS_BIOS_AN_ROMS    \
	/* v0a.7e, New boardset bios, version A for Japan */ \
\
	ROM_REGION( 0x10000, "maincpu", 0 ) \
	ROM_LOAD( "v0-a.7e",    0xf000, 0x1000, CRC(3d33ac34) SHA1(909d59e7a993affd10224402b4370e82a5f5545c) ) /* from RMS-8 board: 2732 EPROM @7E w/'V0A-' label (has HDRA01HDR string inside it), bios code */ \
\
	DECOCASS_COMMON_ROMS

#define DECOCASS_BIOS_BN_ROMS    \
	/* rms8.7e, New boardset bios, version B for US */ \
\
	ROM_REGION( 0x10000, "maincpu", 0 ) \
	ROM_LOAD( "v0-b.7e",    0xf000, 0x1000, CRC(23d929b7) SHA1(063f83020ba3d6f43ab8471f95ca919767b93aa4) ) /* from RMS-8 board: 2732 EPROM @7E w/'V0B-' label (has HDRB01HDR string inside it), bios code */ \
\
	DECOCASS_COMMON_ROMS
	
#define DECOCASS_BIOS_CN_ROMS    \
	/* rms8.7e, New boardset bios, version C for UK */ \
\
	ROM_REGION( 0x10000, "maincpu", 0 ) \
	ROM_LOAD( "v0-c.7e",    0xf000, 0x1000, CRC(9f505709) SHA1(a9c661ba5a0d3fa5e935fb9c10fa63e2d9809981) ) /* from RMS-8 board: 2732 EPROM @7E w/'V0C-' label (has HDRC01HDR string inside it), bios code */ \
\
	DECOCASS_COMMON_ROMS

#define DECOCASS_BIOS_DN_ROMS    \
	/* v0d.7e, New boardset bios, version D for Europe (except UK) */ \
\
	ROM_REGION( 0x10000, "maincpu", 0 ) \
	ROM_LOAD( "v0-d.7e",    0xf000, 0x1000, CRC(1e0c22b1) SHA1(5fec8fef500bbebc13d0173406afc55235d3affb) ) /* from RMS-8 board: 2732 EPROM @7E w/'V0D-' label (has HDRD01HDR string inside it), bios code */ \
\
	DECOCASS_COMMON_ROMS


#define DECOCASS_BIOS_B0_ROMS   \
	/* dsp3.p0b/p1b, Old boardset bios, version B for USA; from DSP-3 board? has HDRB01x string in it, 2x 2716 EPROM? */ \
\
	ROM_REGION( 0x10000, "maincpu", 0 ) \
	ROM_LOAD( "dsp3.p0b",   0xf000, 0x0800, CRC(b67a91d9) SHA1(681c040be0f0ed1ba0a50161b36d0ad8e1c8c5cb) ) \
	ROM_LOAD( "dsp3.p1b",   0xf800, 0x0800, CRC(3bfff5f3) SHA1(4e9437cb1b76d64da6b37f01bd6e879fb399e8ce) ) \
\
	DECOCASS_COMMON_ROMS

ROM_START( decocass )
	DECOCASS_BIOS_BN_ROMS

ROM_END

/* The Following use Dongle Type 1 (DE-0061)
    (dongle data same for each game)         */

/* Test tapes are using a dongle from another game which number is indicated by the third and fourth digits of the serial number, as second digit is always 9 */
ROM_START( ctsttape )
	DECOCASS_BIOS_BN_ROMS

	ROM_REGION( 0x00020, "dongle", 0 )    /* dongle data */
	ROM_LOAD( "de-0061.pro",     0x0000, 0x0020, CRC(e09ae5de) SHA1(7dec067d0739a6dad2607132641b66880a5b7751) )

	ROM_REGION( 0x10000, "cassette", 0 )  /* (max) 64k for cassette image */
	ROM_LOAD( "testtape.cas",    0x0000, 0x2000, CRC(4f9d8efb) SHA1(5b77747dad1033e5703f06c0870441b54b4256c5) )
ROM_END

/* 01 HWY Chase (World) */
ROM_START( chwych0a ) // version MD 0-A-0 verified, 104 blocks, decrypted main data CRC(f0f26b29)
	DECOCASS_BIOS_AN_ROMS

	ROM_REGION( 0x10000, "cassette", 0 )  /* (max) 64k for cassette image */
	ROM_LOAD( "dt-1010-a-0.cas", 0x0000, 0x6900, CRC(6bbbacbc) SHA1(a56842715cb751405794086fcaea2401374f7367) )

	ROM_REGION( 0x00020, "dongle", 0 )    /* dongle data */
	ROM_LOAD( "dp-1000-a.rom",   0x0000, 0x0020, CRC(1bc9fccb) SHA1(ffc59c7660d5c87a8deca294f80260b6bc7c3027) )

ROM_END

ROM_START( chwych1b ) // version MD 1-B-0 not verified (need redump for verification, might be a convert from A version), 103 blocks, decrypted main data CRC(6d7842a5)
	DECOCASS_BIOS_BN_ROMS

	ROM_REGION( 0x10000, "cassette", 0 )  /* (max) 64k for cassette image */
	ROM_LOAD( "dt-1011-b-0.cas", 0x0000, 0x6800, CRC(ebb21163) SHA1(93c87ef303b5ed669291f62393d50be5069aff47) )

	ROM_REGION( 0x00020, "dongle", 0 )    /* dongle data */
	ROM_LOAD( "dp-1000-b.rom",   0x0000, 0x0020, CRC(e09ae5de) SHA1(7dec067d0739a6dad2607132641b66880a5b7751) )

ROM_END

/* 02 Sengoku Ninjatai (Japan), Ninja (World) */
ROM_START( cninjt0a ) // version MD 0-A-0 verified, 100 blocks, decrypted main data CRC(b838ee76)
	DECOCASS_BIOS_AN_ROMS

	ROM_REGION( 0x10000, "cassette", 0 )  /* (max) 64k for cassette image */
	ROM_LOAD( "dt-1020-a-0.cas", 0x0000, 0x6500, CRC(e2964e5b) SHA1(74c8475391a363f523368ab3b67d79034bf2c747) )

	ROM_REGION( 0x00020, "dongle", 0 )    /* dongle data */
	ROM_LOAD( "dp-1000-a.rom",   0x0000, 0x0020, CRC(1bc9fccb) SHA1(ffc59c7660d5c87a8deca294f80260b6bc7c3027) )

ROM_END

ROM_START( cninjt1d ) // version MD 1-D-0 verified (might be a convert from A version), 100 blocks, decrypted main data CRC(6d7842a5)
	DECOCASS_BIOS_DN_ROMS

	ROM_REGION( 0x10000, "cassette", 0 )  /* (max) 64k for cassette image */
	ROM_LOAD( "dt-1021-d-0.cas", 0x0000, 0x6500, CRC(f970f436) SHA1(a85534f1bc8c7a4eceae8a095d178a3c6795cdee) )

	ROM_REGION( 0x00020, "dongle", 0 )    /* dongle data */
	ROM_LOAD( "dp-1000-d.rom",   0x0000, 0x0020, CRC(57383f41) SHA1(aae91284b78eb297475a0e1882479c681fcb6c49) )

ROM_END

/* 03 Manhattan (World) */
ROM_START( cmanht0a ) // version MD 0-A-0 not verified, 095 blocks, decrypted main data CRC(b1fe47cd)
	DECOCASS_BIOS_AN_ROMS

	ROM_REGION( 0x10000, "cassette", 0 )  /* (max) 64k for cassette image */
	ROM_LOAD( "dt-1020-a-0.cas", 0x0000, 0x6000, CRC(59983936) SHA1(10a6785b9e126afd880090b163563fcb47b2fd96) )

	ROM_REGION( 0x00020, "dongle", 0 )    /* dongle data */
	ROM_LOAD( "dp-1000-a.rom",   0x0000, 0x0020, CRC(1bc9fccb) SHA1(ffc59c7660d5c87a8deca294f80260b6bc7c3027) )

ROM_END

ROM_START( cmanht1a ) // version MD 1-A-0 verified, 095 blocks, decrypted main data CRC(273c1535)
	DECOCASS_BIOS_AN_ROMS

	ROM_REGION( 0x10000, "cassette", 0 )  /* (max) 64k for cassette image */
	ROM_LOAD( "dt-1021-d-0.cas", 0x0000, 0x6000, CRC(92dae2b1) SHA1(cc048ac6601553675078230290beb3d59775bfe0) )

	ROM_REGION( 0x00020, "dongle", 0 )    /* dongle data */
	ROM_LOAD( "dp-1000-d.rom",   0x0000, 0x0020, CRC(1bc9fccb) SHA1(ffc59c7660d5c87a8deca294f80260b6bc7c3027) )

ROM_END

ROM_START( cmanht2a ) // version MD 2-A-0 verified, 095 blocks, decrypted main data CRC(ac979eee)
	DECOCASS_BIOS_AN_ROMS

	ROM_REGION( 0x10000, "cassette", 0 )  /* (max) 64k for cassette image */
	ROM_LOAD( "dt-1021-d-0.cas", 0x0000, 0x6000, CRC(f19ffcda) SHA1(64a4762901a683f03617948d5658fe9581338bb8) )

	ROM_REGION( 0x00020, "dongle", 0 )    /* dongle data */
	ROM_LOAD( "dp-1000-d.rom",   0x0000, 0x0020, CRC(1bc9fccb) SHA1(ffc59c7660d5c87a8deca294f80260b6bc7c3027) )

ROM_END

/* 04 Terranean (World) */
ROM_START( cterra2a ) // version MD 2-A-0 verified, 104 blocks, decrypted main data CRC(5978ca00)
	DECOCASS_BIOS_AN_ROMS

	ROM_REGION( 0x10000, "cassette", 0 )   /* (max) 64k for cassette image */
	ROM_LOAD( "dt-1042-a.cas",   0x0000, 0x6900, CRC(d7dae636) SHA1(a00e8e74a8f416ece44933077c6ac60e57181120) )

	ROM_REGION( 0x00020, "dongle", 0 )    /* dongle data */
	ROM_LOAD( "dp-1000-a.rom",   0x0000, 0x0020, CRC(1bc9fccb) SHA1(ffc59c7660d5c87a8deca294f80260b6bc7c3027) )
ROM_END

ROM_START( cterra2d ) // version MD 2-D-0 verified, 104 blocks, decrypted main data CRC(013e7750), 1 bit difference with A version
	DECOCASS_BIOS_DN_ROMS

	ROM_REGION( 0x10000, "cassette", 0 )  /* (max) 64k for cassette image */
	ROM_LOAD( "dt-1042-d.cas",   0x0000, 0x6900, CRC(3b4961ce) SHA1(871d19a1203f828a43103162c043f63ed44c2105) )

	ROM_REGION( 0x00020, "dongle", 0 )    /* dongle data */
	ROM_LOAD( "dp-1000-d.rom",   0x0000, 0x0020, CRC(57383f41) SHA1(aae91284b78eb297475a0e1882479c681fcb6c49) )
ROM_END

ROM_START( cterra4a ) // version MD 4-A-0 not verified, 105 blocks, decrypted main data CRC(dcce263c)
	DECOCASS_BIOS_AN_ROMS

	ROM_REGION( 0x10000, "cassette", 0 )  /* (max) 64k for cassette image */
	ROM_LOAD( "dt-1044-a.cas",   0x0000, 0x6a00, CRC(3f5c7bd8) SHA1(2e88a59386c63523c041065b753e132b61b4e306) )

	ROM_REGION( 0x00020, "dongle", 0 )    /* dongle data */
	ROM_LOAD( "dp-1000-a.rom",   0x0000, 0x0020, CRC(1bc9fccb) SHA1(ffc59c7660d5c87a8deca294f80260b6bc7c3027) )
ROM_END

ROM_START( cterra4b ) // version MD 4-B-0 not verified, 105 blocks, decrypted main data CRC(dcce263c)
	DECOCASS_BIOS_BN_ROMS

	ROM_REGION( 0x10000, "cassette", 0 )  /* (max) 64k for cassette image */
	ROM_LOAD( "dt-1044-b.cas",   0x0000, 0x6a00, CRC(e1d4846c) SHA1(893f81d32b74e82a61f19bd3298ff964834cddd7) )

	ROM_REGION( 0x00020, "dongle", 0 )    /* dongle data */
	ROM_LOAD( "dp-1000-b.rom",   0x0000, 0x0020, CRC(e09ae5de) SHA1(7dec067d0739a6dad2607132641b66880a5b7751) )
ROM_END

/* 06 Nebula (World) */
ROM_START( cnebul2a ) // version MD 2-A-0 verified, 091 blocks, decrypted main data CRC(6eac1650)
	DECOCASS_BIOS_AN_ROMS

	ROM_REGION( 0x10000, "cassette", 0 )  /* (max) 64k for cassette image */
	ROM_LOAD( "dt-1062-a.cas",   0x0000, 0x5c00, CRC(c457f713) SHA1(9de093d9768180a466286f5ef48d40eb901cab7b) )

	ROM_REGION( 0x00020, "dongle", 0 )    /* dongle data */
	ROM_LOAD( "dp-1000-a.rom",   0x0000, 0x0020, CRC(1bc9fccb) SHA1(ffc59c7660d5c87a8deca294f80260b6bc7c3027) )
ROM_END

/* 07 Astro Fantasia (World) */
ROM_START( castro6a ) // version MD 6-A-0 verified, 104 blocks, decrypted main data CRC(b5c3bc9b)
	DECOCASS_BIOS_AN_ROMS

	ROM_REGION( 0x10000, "cassette", 0 )  /* (max) 64k for cassette image */
	ROM_LOAD( "castro6a.cas",   0x0000, 0x6900, CRC(6d3ab1c3) SHA1(b201891623533df9c899107786b11e8e82224735) )

	ROM_REGION( 0x00020, "dongle", 0 )    /* dongle data */
	ROM_LOAD( "dp-1000-a.rom",   0x0000, 0x0020, CRC(1bc9fccb) SHA1(ffc59c7660d5c87a8deca294f80260b6bc7c3027) )
ROM_END

ROM_START( castro6b ) // version MD 6-B-0 verified, 104 blocks, decrypted main data CRC(b5C3bc9b)
	DECOCASS_BIOS_BN_ROMS

	ROM_REGION( 0x10000, "cassette", 0 )  /* (max) 64k for cassette image */
	ROM_LOAD( "castro6b.cas",    0x0000, 0x6900, CRC(e2a4fd76) SHA1(3664f05bea493eeef03e74a0be1f3267c2f2136f) )

	ROM_REGION( 0x00020, "dongle", 0 )    /* dongle data */
	ROM_LOAD( "dp-1000-a.rom",   0x0000, 0x0020, CRC(e09ae5de) SHA1(7dec067d0739a6dad2607132641b66880a5b7751) )
ROM_END

ROM_START( castro7a ) // version MD 7-A-0 verified, 105 blocks, decrypted main data CRC(d9260c6e)
	DECOCASS_BIOS_AN_ROMS

	ROM_REGION( 0x10000, "cassette", 0 )  /* (max) 64k for cassette image */
	ROM_LOAD( "castro7a.cas",    0x0000, 0x6a00, CRC(748d750b) SHA1(4a8239e2c665b12c8293032b4f905f286116914c) )

	ROM_REGION( 0x00020, "dongle", 0 )    /* dongle data */
	ROM_LOAD( "dp-1000-a.rom",   0x0000, 0x0020, CRC(1bc9fccb) SHA1(ffc59c7660d5c87a8deca294f80260b6bc7c3027) )
ROM_END

ROM_START( castro7b ) // version MD 7-B-0 verified, 105 blocks, decrypted main data CRC(d9260c6e)
	DECOCASS_BIOS_BN_ROMS

	ROM_REGION( 0x10000, "cassette", 0 )  /* (max) 64k for cassette image */
	ROM_LOAD( "castro7b.cas",    0x0000, 0x6a00, CRC(85182551) SHA1(64cdd47c54f8cbb84c25605005eab0f390330f9a) )

	ROM_REGION( 0x00020, "dongle", 0 )    /* dongle data */
	ROM_LOAD( "dp-1000-a.rom",   0x0000, 0x0020, CRC(e09ae5de) SHA1(7dec067d0739a6dad2607132641b66880a5b7751) )
ROM_END

ROM_START( castro4c ) // version MD 4-C-0 verified, 104 blocks, decrypted main data CRC(03eb00ee)
	DECOCASS_BIOS_CN_ROMS

	ROM_REGION( 0x10000, "cassette", 0 )  /* (max) 64k for cassette image */
	ROM_LOAD( "castro4c.cas",    0x0000, 0x6900, CRC(8565c89e) SHA1(34a6b31546dec54808d177d5e2888896f2ae8b8b) )

	ROM_REGION( 0x00020, "dongle", 0 )    /* dongle data */
	ROM_LOAD( "dp-1000-a.rom",   0x0000, 0x0020, CRC(095953c8) SHA1(74e898f305da38dab639d7fc1d22a2db2b1cc2b9) )
ROM_END

/* 08 The Tower (World) */
ROM_START( ctower0a ) // version MD 0-A-0 verified, 104 blocks, decrypted main data CRC(ae729432)
	DECOCASS_BIOS_AN_ROMS

	ROM_REGION( 0x10000, "cassette", 0 )  /* (max) 64k for cassette image */
	ROM_LOAD( "dt-1080-a.cas",   0x0000, 0x6900, CRC(6fee3ba6) SHA1(3cf3d8becaf3e55045dd58fd3e1d51e786a44391) )

	ROM_REGION( 0x00020, "dongle", 0 )    /* dongle data */
	ROM_LOAD( "dp-1000-a.rom",   0x0000, 0x0020, CRC(1bc9fccb) SHA1(ffc59c7660d5c87a8deca294f80260b6bc7c3027) )
ROM_END

/* 09 Super Astro Fighter (World) */
ROM_START( csastf4a ) // version MD 4-A-0 verified, 094 blocks, decrypted main data CRC(e334ed6f)
	DECOCASS_BIOS_AN_ROMS

	ROM_REGION( 0x10000, "cassette", 0 )  /* (max) 64k for cassette image */
	ROM_LOAD( "dt-1094-a-0.cas", 0x0000, 0x5f00, CRC(5e86c2a5) SHA1(e0aafe1db67b9fd52570f9029c10fadcd8cf1404) )
	
	ROM_REGION( 0x00020, "dongle", 0 )    /* dongle data */
	ROM_LOAD( "dp-1000-a.rom",   0x0000, 0x0020, CRC(1bc9fccb) SHA1(ffc59c7660d5c87a8deca294f80260b6bc7c3027) )
ROM_END

ROM_START( csastf4b ) // version MD 4-B-0 verified, 094 blocks, decrypted main data CRC(e334ed6f)
	DECOCASS_BIOS_BN_ROMS

	ROM_REGION( 0x10000, "cassette", 0 )  /* (max) 64k for cassette image */
	ROM_LOAD( "dt-1094-b-0.cas", 0x0000, 0x5f00, CRC(95574a9b) SHA1(cc2d4a328a3279e7b641effb42098b5dd02e0c82) )
	
	ROM_REGION( 0x00020, "dongle", 0 )    /* dongle data */
	ROM_LOAD( "dp-1000-b.rom",   0x0000, 0x0020, CRC(e09ae5de) SHA1(7dec067d0739a6dad2607132641b66880a5b7751) )
ROM_END

ROM_START( csastf2c ) // version MD 2-C-0 verified, 092 blocks, decrypted main data CRC(ea665fbb)
	DECOCASS_BIOS_CN_ROMS

	ROM_REGION( 0x10000, "cassette", 0 )  /* (max) 64k for cassette image */
	ROM_LOAD( "dt-1092-c-0.cas", 0x0000, 0x5d00, CRC(c9b8e154) SHA1(78ed3e1398ba8603850d0f578fc97f26a48532bc) )
	
	ROM_REGION( 0x00020, "dongle", 0 )    /* dongle data */
	ROM_LOAD( "dp-1000-c.rom",   0x0000, 0x0020, CRC(095953c8) SHA1(74e898f305da38dab639d7fc1d22a2db2b1cc2b9) )
ROM_END

ROM_START( csastf3b ) // version MD 3-B-0 verified, 094 blocks, decrypted main data CRC(52e8d339)
	DECOCASS_BIOS_BN_ROMS

	ROM_REGION( 0x10000, "cassette", 0 )  /* (max) 64k for cassette image */
	ROM_LOAD( "dt-1093-b-0.cas", 0x0000, 0x5f00, CRC(92184930) SHA1(16a764be11cd8e002126e49769ce7eced70ec7ef) )
	
	ROM_REGION( 0x00020, "dongle", 0 )    /* dongle data */
	ROM_LOAD( "dp-1000-b.rom",   0x0000, 0x0020, CRC(e09ae5de) SHA1(7dec067d0739a6dad2607132641b66880a5b7751) )
ROM_END

/* 11 Lock'n'Chase (World) */
ROM_START( clocknch )
	DECOCASS_BIOS_BN_ROMS

	ROM_REGION( 0x00020, "dongle", 0 )    /* dongle data */
	ROM_LOAD( "dp-1110_b.dgl",   0x0000, 0x0020, CRC(e09ae5de) SHA1(7dec067d0739a6dad2607132641b66880a5b7751) )

	ROM_REGION( 0x10000, "cassette", 0 )      /* (max) 64k for cassette image */
	ROM_LOAD( "clocknch.cas",    0x0000, 0x8000, CRC(c9d163a4) SHA1(3ef55a8d8f603059e263776c08eb81f2cf18b75c) )
ROM_END

/* 13 Pro Golf (World), Tournament Pro Golf (World), 18 Challenge Pro Golf (World) */
ROM_START( cpgolf1a ) // version MD 1-A-0 verified, 099 blocks, decrypted main data CRC(4f713213)
	DECOCASS_BIOS_AN_ROMS

	ROM_REGION( 0x10000, "cassette", 0 )  /* (max) 64k for cassette image */
	ROM_LOAD( "dt-1131-a-0.cas", 0x0000, 0x6400, CRC(b1f7f304) SHA1(e9e5e9363e239a064fa3da54a54e01bb5ba92a93) )

	ROM_REGION( 0x00020, "dongle", 0 )    /* decryption rom from dongle */
	ROM_LOAD( "dp-1000-a.rom",   0x0000, 0x0020, CRC(1bc9fccb) SHA1(ffc59c7660d5c87a8deca294f80260b6bc7c3027) )
ROM_END

ROM_START( cpgolf1b ) // version MD 1-B-0 verified, 099 blocks, decrypted main data CRC(4f713213)
	DECOCASS_BIOS_BN_ROMS

	ROM_REGION( 0x10000, "cassette", 0 )  /* (max) 64k for cassette image */
	ROM_LOAD( "dt-1131-b-0.cas", 0x0000, 0x6400, CRC(f1f1e3be) SHA1(1ca40902d30a0fd2b5a1a783b276846088e53112) )

	ROM_REGION( 0x00020, "dongle", 0 )    /* decryption rom from dongle */
	ROM_LOAD( "dp-1000-b.rom",   0x0000, 0x0020, CRC(e09ae5de) SHA1(7dec067d0739a6dad2607132641b66880a5b7751) )
ROM_END

ROM_START( cpgolf4a ) // version MD 4-A-0 verified, 102 blocks, decrypted main data CRC(9ebadcad)
	DECOCASS_BIOS_AN_ROMS

	ROM_REGION( 0x10000, "cassette", 0 )  /* (max) 64k for cassette image */
	ROM_LOAD( "dt-1134-a-0.cas", 0x0000, 0x6700, CRC(3024396c) SHA1(c49d878bae46bf8bf0b0b098a5d94d9ec68b526d) )

	ROM_REGION( 0x00020, "dongle", 0 )    /* decryption rom from dongle */
	ROM_LOAD( "dp-1000-a.rom",   0x0000, 0x0020, CRC(1bc9fccb) SHA1(ffc59c7660d5c87a8deca294f80260b6bc7c3027) )
ROM_END

ROM_START( cpgolf6a ) // version MD 6-A-0 verified, 104 blocks, decrypted main data CRC(3c5418c5)
	DECOCASS_BIOS_AN_ROMS

	ROM_REGION( 0x10000, "cassette", 0 )  /* (max) 64k for cassette image */
	ROM_LOAD( "dt-1136-a-0.cas", 0x0000, 0x6900, CRC(8441136f) SHA1(7b7702d7b7f094b709fc4503f3d55d3cea59a9b1) )

	ROM_REGION( 0x00020, "dongle", 0 )    /* decryption rom from dongle */
	ROM_LOAD( "dp-1000-a.rom",   0x0000, 0x0020, CRC(1bc9fccb) SHA1(ffc59c7660d5c87a8deca294f80260b6bc7c3027) )
ROM_END

ROM_START( cpgolf7d ) // version MD 7-D-0 verified, 100 blocks, decrypted main data CRC(b5a12cb8)
	DECOCASS_BIOS_DN_ROMS

	ROM_REGION( 0x10000, "cassette", 0 )  /* (max) 64k for cassette image */
	ROM_LOAD( "dt-1137-d-0.cas", 0x0000, 0x6500, CRC(9abea0f4) SHA1(8ffe16ebfa98cbe2d3d00fdcac446e3949e7c9e9) )

	ROM_REGION( 0x00020, "dongle", 0 )    /* decryption rom from dongle */
	ROM_LOAD( "dp-1000-d.rom",   0x0000, 0x0020, CRC(57383f41) SHA1(aae91284b78eb297475a0e1882479c681fcb6c49) )
ROM_END

ROM_START( cpgolf9b ) // version MD 9-B-0 verified, 102 blocks, decrypted main data CRC(6f6d904d)
	DECOCASS_BIOS_BN_ROMS

	ROM_REGION( 0x10000, "cassette", 0 )  /* (max) 64k for cassette image */
	ROM_LOAD( "dt-1139-b-0.cas", 0x0000, 0x6700, CRC(5a535ae5) SHA1(12234d11b68beb450148d52f0103103101b6d919) )

	ROM_REGION( 0x00020, "dongle", 0 )    /* decryption rom from dongle */
	ROM_LOAD( "dp-1000-b.rom",   0x0000, 0x0020, CRC(e09ae5de) SHA1(7dec067d0739a6dad2607132641b66880a5b7751) )
ROM_END

/* 14 DS Telejan (Japan) */
ROM_START( cdstj2a0 ) // version MD 2-A-0 verified, 115 blocks, decrypted main data CRC(f14b605d)
	DECOCASS_BIOS_AN_ROMS

	ROM_REGION( 0x10000, "cassette", 0 )  /* (max) 64k for cassette image */
	ROM_LOAD( "dt-1142-a-0.cas", 0x0000, 0x7400, CRC(2c3f9183) SHA1(7b5e1445353b7785f4bf95ac8bdc2e98fb13656a) )

	ROM_REGION( 0x00020, "dongle", 0 )    /* dongle data */
	ROM_LOAD( "dp-1000-a.rom",   0x0000, 0x0020, CRC(1bc9fccb) SHA1(ffc59c7660d5c87a8deca294f80260b6bc7c3027) )
ROM_END

ROM_START( cdstj3a0 ) // version MD 3-A-0 verified, 115 blocks, decrypted main data CRC(812e5222)
	DECOCASS_BIOS_AN_ROMS

	ROM_REGION( 0x10000, "cassette", 0 )  /* (max) 64k for cassette image */
	ROM_LOAD( "dt-1143-a-0.cas", 0x0000, 0x7400, CRC(c842885c) SHA1(c5af43c6c3a0284540b25bd594c7bde4fc92199b) )

	ROM_REGION( 0x00020, "dongle", 0 )    /* dongle data */
	ROM_LOAD( "dp-1000-a.rom",   0x0000, 0x0020, CRC(1bc9fccb) SHA1(ffc59c7660d5c87a8deca294f80260b6bc7c3027) )
ROM_END

ROM_START( cdstj4a2 ) // version MD 4-A-2 verified, 114 blocks, decrypted main data CRC(c0508930)
	DECOCASS_BIOS_AN_ROMS

	ROM_REGION( 0x10000, "cassette", 0 )  /* (max) 64k for cassette image */
	ROM_LOAD( "dt-1144-a-2.cas", 0x0000, 0x7300, CRC(72fa9d71) SHA1(a1682b8635f960db5c970632c8796f63d0d67f57) )

	ROM_REGION( 0x00020, "dongle", 0 )    /* dongle data */
	ROM_LOAD( "dp-1000-a.rom",   0x0000, 0x0020, CRC(1bc9fccb) SHA1(ffc59c7660d5c87a8deca294f80260b6bc7c3027) )
ROM_END

ROM_START( cdstj4a3 ) // version MD 4-A-3 verified, 114 blocks, decrypted main data CRC(eb256274)
	DECOCASS_BIOS_AN_ROMS

	ROM_REGION( 0x10000, "cassette", 0 )  /* (max) 64k for cassette image */
	ROM_LOAD( "dt-1144-a-3.cas", 0x0000, 0x7300, CRC(1336a912) SHA1(0c64e069713b411da38b43f14306953621726d35) )

	ROM_REGION( 0x00020, "dongle", 0 )    /* dongle data */
	ROM_LOAD( "dp-1000-a.rom",   0x0000, 0x0020, CRC(1bc9fccb) SHA1(ffc59c7660d5c87a8deca294f80260b6bc7c3027) )
ROM_END

/* 15 Lucky Poker (World) */
/* Photo of Dongle shows DP-1150B (the "B" is in a separate white box then the DP-1150 label) */
ROM_START( cluckypo )
	DECOCASS_BIOS_BN_ROMS

	ROM_REGION( 0x00020, "dongle", 0 )    /* dongle data */
	ROM_LOAD( "dp-1150_b.dgl",   0x0000, 0x0020, CRC(e09ae5de) SHA1(7dec067d0739a6dad2607132641b66880a5b7751) )

	ROM_REGION( 0x10000, "cassette", 0 )      /* (max) 64k for cassette image */
	ROM_LOAD( "cluckypo.cas",    0x0000, 0x8000, CRC(2070c243) SHA1(cd3af309af8eb27937756c1fe6fd0504be5aaaf5) )
ROM_END

/* 16 Treasure Island (World) */
ROM_START( ctisland )
	DECOCASS_BIOS_BN_ROMS

	ROM_REGION( 0x00020, "dongle", 0 )    /* dongle data */
	ROM_LOAD( "de-0061.pro",     0x0000, 0x0020, CRC(e09ae5de) SHA1(7dec067d0739a6dad2607132641b66880a5b7751) )

	ROM_REGION( 0x10000, "cassette", 0 )      /* (max) 64k for cassette image */
	ROM_LOAD( "ctisland.cas",    0x0000, 0x8000, CRC(3f63b8f8) SHA1(2fd0679ef9750a228ebb098672ab6091fda75804) )

	ROM_REGION( 0x4000, "user3", 0 )      /* roms from the overlay pcb */
	ROM_LOAD( "deco-ti.x1",      0x0000, 0x1000, CRC(a7f8aeba) SHA1(0c9ba1a46d0636b36f40fad31638db89f374f778) )
	ROM_LOAD( "deco-ti.x2",      0x1000, 0x1000, CRC(2a0d3c91) SHA1(552d08fcddddbea5b52fa1e8decd188ae49c86ea) )
	ROM_LOAD( "deco-ti.x3",      0x2000, 0x1000, CRC(3a26b97c) SHA1(f57e76077806e149a9e455c85e5431eac2d42bc3) )
	ROM_LOAD( "deco-ti.x4",      0x3000, 0x1000, CRC(1cbe43de) SHA1(8f26ad224e96c87da810c60d3dd88d415400b9fc) )
ROM_END

ROM_START( ctisland2 )
	DECOCASS_BIOS_BN_ROMS

	ROM_REGION( 0x00020, "dongle", 0 )    /* dongle data */
	ROM_LOAD( "de-0061.pro",     0x0000, 0x0020, CRC(e09ae5de) SHA1(7dec067d0739a6dad2607132641b66880a5b7751) )

	ROM_REGION( 0x10000, "cassette", 0 )      /* (max) 64k for cassette image */
	ROM_LOAD( "ctislnd2.cas",    0x0000, 0x8000, CRC(2854b4c0) SHA1(d3b4e0031dbb2340fbbe396a1ff9b8fbfd63663e) )

	ROM_REGION( 0x4000, "user3", 0 )      /* roms from the overlay pcb */
	ROM_LOAD( "deco-ti.x1",      0x0000, 0x1000, CRC(a7f8aeba) SHA1(0c9ba1a46d0636b36f40fad31638db89f374f778) )
	ROM_LOAD( "deco-ti.x2",      0x1000, 0x1000, CRC(2a0d3c91) SHA1(552d08fcddddbea5b52fa1e8decd188ae49c86ea) )
	ROM_LOAD( "deco-ti.x3",      0x2000, 0x1000, CRC(3a26b97c) SHA1(f57e76077806e149a9e455c85e5431eac2d42bc3) )
	ROM_LOAD( "deco-ti.x4",      0x3000, 0x1000, CRC(1cbe43de) SHA1(8f26ad224e96c87da810c60d3dd88d415400b9fc) )
ROM_END

ROM_START( ctisland3 )
	DECOCASS_BIOS_BN_ROMS

	ROM_REGION( 0x00020, "dongle", 0 )    /* dongle data */
	ROM_LOAD( "de-0061.pro",     0x0000, 0x0020, CRC(e09ae5de) SHA1(7dec067d0739a6dad2607132641b66880a5b7751) )

	ROM_REGION( 0x10000, "cassette", 0 )      /* (max) 64k for cassette image */
	ROM_LOAD( "ctislnd3.cas",    0x0000, 0x8000, CRC(45464e1e) SHA1(03275694d963c7ab0e0f5525e248e69da5f9b591) )

	ROM_REGION( 0x4000, "user3", 0 )      /* roms from the overlay pcb */
	ROM_LOAD( "deco-ti.x1",      0x0000, 0x1000, CRC(a7f8aeba) SHA1(0c9ba1a46d0636b36f40fad31638db89f374f778) )
	ROM_LOAD( "deco-ti.x2",      0x1000, 0x1000, CRC(2a0d3c91) SHA1(552d08fcddddbea5b52fa1e8decd188ae49c86ea) )
	ROM_LOAD( "deco-ti.x3",      0x2000, 0x1000, CRC(3a26b97c) SHA1(f57e76077806e149a9e455c85e5431eac2d42bc3) )
	ROM_LOAD( "deco-ti.x4",      0x3000, 0x1000, CRC(1cbe43de) SHA1(8f26ad224e96c87da810c60d3dd88d415400b9fc) )
ROM_END

/* 18 Explorer (World) */
ROM_START( cexplr0a ) // version MD 0-A-0 verified, 104 blocks, decrypted main data CRC(673D1F0D)
	DECOCASS_BIOS_AN_ROMS

	ROM_REGION( 0x10000, "cassette", 0 )  /* (max) 64k for cassette image */
	ROM_LOAD( "dt-1180-a-0.cas", 0x0000, 0x6900, CRC(f96eef1f) SHA1(a8b2bbd6148d9144a9fe1e08380f015eb443f8a9) )

	ROM_REGION( 0x00020, "dongle", 0 )    /* decryption rom from dongle */
	ROM_LOAD( "dp-1000-a.rom",   0x0000, 0x0020, CRC(1bc9fccb) SHA1(ffc59c7660d5c87a8deca294f80260b6bc7c3027) )

	ROM_REGION( 0x5000, "user3", 0 )      /* roms from the overlay pcb, first row (need redump for confirmation) */
    ROM_LOAD( "gr0_18.x1",       0x0000, 0x1000, CRC(f2ca58f0) SHA1(5c9faeca6247b70586dc2a3765805ac96960ac79) )
	ROM_LOAD( "gr0_18.x2",       0x1000, 0x1000, CRC(75d999bf) SHA1(7c257285d5b69642ec542dc56defdbb1f2072454) )
	ROM_LOAD( "gr0_18.x3",       0x2000, 0x1000, CRC(941539c6) SHA1(2e879107f56bf258ad90fb83c2ab278027acb0bb) )
	ROM_LOAD( "gr0_18.x4",       0x3000, 0x1000, CRC(73388544) SHA1(9c98f79e431d0881e20eac4c6c4177db8973ce20) )
	ROM_LOAD( "gr0_18.x5",       0x4000, 0x1000, CRC(b40699c5) SHA1(4934283d2845dbd3ea9a7fa349f663a34fcdfdf8) )

	ROM_REGION( 0x5000, "user4", 0 )      /* roms from the overlay pcb, second row (need redump for confirmation) */
    ROM_LOAD( "gr0_18.y1",       0x0000, 0x1000, CRC(d887dc50) SHA1(9321e40d208bd029657ab87eaf815f8a09e49b48) )
	ROM_LOAD( "gr0_18.y2",       0x1000, 0x1000, CRC(fe325d0d) SHA1(3e4aaba87e2aa656346169d512d70083605692c6) )
	ROM_LOAD( "gr0_18.y3",       0x2000, 0x1000, CRC(7a787ecf) SHA1(5261747823b58be3fabb8d1a8cb4069082f95b30) )
	ROM_LOAD( "gr0_18.y4",       0x3000, 0x1000, CRC(ac30e8c7) SHA1(f8f53b982df356e5bf2624afe0f8a72635b3b4b3) )
	ROM_LOAD( "gr0_18.y5",       0x4000, 0x1000, CRC(0a6b8f03) SHA1(09b477579a5fed4c45299b6366141ef4a8c9a410) )
ROM_END

ROM_START( cexplr0b ) // version MD 0-B-0 unverified (need redump), 104 blocks, decrypted main data CRC(673D1F0D)
	DECOCASS_BIOS_BN_ROMS

	ROM_REGION( 0x10000, "cassette", 0 )  /* (max) 64k for cassette image (need redump for confirmation) */
	ROM_LOAD( "dt-1180-b-0.cas", 0x0000, 0x6900, CRC(3b912c8a) SHA1(e69212d84e2507ff44e49b8e5586f75e9431fb6c) )

	ROM_REGION( 0x00020, "dongle", 0 )    /* decryption rom from dongle */
	ROM_LOAD( "dp-1000-b.rom",   0x0000, 0x0020, CRC(e09ae5de) SHA1(7dec067d0739a6dad2607132641b66880a5b7751) )

	ROM_REGION( 0x5000, "user3", 0 )      /* roms from the overlay pcb, first row (need redump for confirmation) */
    ROM_LOAD( "gr0_18.x1",       0x0000, 0x1000, CRC(f2ca58f0) SHA1(5c9faeca6247b70586dc2a3765805ac96960ac79) )
	ROM_LOAD( "gr0_18.x2",       0x1000, 0x1000, CRC(75d999bf) SHA1(7c257285d5b69642ec542dc56defdbb1f2072454) )
	ROM_LOAD( "gr0_18.x3",       0x2000, 0x1000, CRC(941539c6) SHA1(2e879107f56bf258ad90fb83c2ab278027acb0bb) )
	ROM_LOAD( "gr0_18.x4",       0x3000, 0x1000, CRC(73388544) SHA1(9c98f79e431d0881e20eac4c6c4177db8973ce20) )
	ROM_LOAD( "gr0_18.x5",       0x4000, 0x1000, CRC(b40699c5) SHA1(4934283d2845dbd3ea9a7fa349f663a34fcdfdf8) )

	ROM_REGION( 0x5000, "user4", 0 )      /* roms from the overlay pcb, second row (need redump for confirmation) */
    ROM_LOAD( "gr0_18.y1",       0x0000, 0x1000, CRC(d887dc50) SHA1(9321e40d208bd029657ab87eaf815f8a09e49b48) )
	ROM_LOAD( "gr0_18.y2",       0x1000, 0x1000, CRC(fe325d0d) SHA1(3e4aaba87e2aa656346169d512d70083605692c6) )
	ROM_LOAD( "gr0_18.y3",       0x2000, 0x1000, CRC(7a787ecf) SHA1(5261747823b58be3fabb8d1a8cb4069082f95b30) )
	ROM_LOAD( "gr0_18.y4",       0x3000, 0x1000, CRC(ac30e8c7) SHA1(f8f53b982df356e5bf2624afe0f8a72635b3b4b3) )
	ROM_LOAD( "gr0_18.y5",       0x4000, 0x1000, CRC(0a6b8f03) SHA1(09b477579a5fed4c45299b6366141ef4a8c9a410) )
ROM_END

/* The Following use Dongle Type 2 (CS82-007)
    (dongle data differs for each game)      */

/* 19 Disco No.1 (Japan/World), Sweet Heart (World) */
ROM_START( cdiscon1 )
/* Photo of Dongle shows DP-1190B (the "B" is in a separate white box then the DP-1190 label) */
	DECOCASS_BIOS_BN_ROMS

	ROM_REGION( 0x00800, "dongle", 0 )    /* dongle data */
	ROM_LOAD( "dp-1190_b.dgl",   0x0000, 0x0800, CRC(0f793fab) SHA1(331f1b1b482fcd10f42c388a503f9af62d705401) )

	ROM_REGION( 0x10000, "cassette", 0 )      /* (max) 64k for cassette image */
	ROM_LOAD( "cdiscon1.cas",    0x0000, 0x8000, CRC(1429a397) SHA1(12f9e03fcda31dc6161a39bf5c3315a1e9e94565) )
ROM_END

ROM_START( csweetht )
	DECOCASS_BIOS_BN_ROMS

	ROM_REGION( 0x00800, "dongle", 0 )   /* dongle data */
	ROM_LOAD( "cdiscon1.pro",    0x0000, 0x0800, CRC(0f793fab) SHA1(331f1b1b482fcd10f42c388a503f9af62d705401) )

	ROM_REGION( 0x10000, "cassette", 0 )   /* (max) 64k for cassette image */
	ROM_LOAD( "csweetht.cas",    0x0000, 0x8000, CRC(175ef706) SHA1(49b86233f69d0daf54a6e59b86e69b8159e8f6cc) )
ROM_END

/* 20 Tornado (World) */
ROM_START( ctornado )
	DECOCASS_BIOS_BN_ROMS

	ROM_REGION( 0x00800, "dongle", 0 )    /* dongle data */
	ROM_LOAD( "ctornado.pro",    0x0000, 0x0800, CRC(c9a91697) SHA1(3f7163291edbdf1a596e3cd2b7a16bbb140ffb36) )

	ROM_REGION( 0x10000, "cassette", 0 )      /* (max) 64k for cassette image */
	ROM_LOAD( "ctornado.cas",    0x0000, 0x8000, CRC(e4e36ce0) SHA1(48a11823121fb2e3de31ae08e453c0124fc4f7f3) )
ROM_END

/* 21 Mission-X (World) */
/* Photo of Dongle shows DP-121B with Cassette DT-1213B (the "3B" is in a separate white box then the DP-121 label) */
ROM_START( cmissnx )
	DECOCASS_BIOS_BN_ROMS

	ROM_REGION( 0x00800, "dongle", 0 )    /* dongle data */
	ROM_LOAD( "dp-121_b.dgl",    0x0000, 0x0800, CRC(8a41c071) SHA1(7b16d933707bf21d25dcd11db6a6c28834b11c5b) )

	ROM_REGION( 0x10000, "cassette", 0 )      /* (max) 64k for cassette image */
	ROM_LOAD( "cmissnx.cas",     0x0000, 0x8000, CRC(3a094e11) SHA1(c355fe14838187cbde19a799e5c60083c82615ac) ) /* Is this the 3B version? */
ROM_END

/* 22 Pro Tennis (World) */
ROM_START( cptennis )
	DECOCASS_BIOS_BN_ROMS

	ROM_REGION( 0x00800, "dongle", 0 )    /* dongle data */
	ROM_LOAD( "cptennis.pro",    0x0000, 0x0800, CRC(59b8cede) SHA1(514861a652b5256a11477fc357bc01dfd87f712b) )

	ROM_REGION( 0x10000, "cassette", 0 )      /* (max) 64k for cassette image */
	ROM_LOAD( "cptennis.cas",    0x0000, 0x8000, CRC(6bb257fe) SHA1(7554bf1996bc9e9c04a276aab050708d70103f54) )
ROM_END


/* The Following use Dongle Type 3 (unknown part number?)
    (dongle data differs for each game)      */

/* 25 Fishing (Japan), Angler Dangler (World) */
ROM_START( cadanglr ) // version 5-B-0
	DECOCASS_BIOS_BN_ROMS

	ROM_REGION( 0x01000, "dongle", 0 )    /* dongle data */
	ROM_LOAD( "dp-1250-a-0.dgl", 0x0000, 0x1000, CRC(92a3b387) SHA1(e17a155d02e9ed806590b23a845dc7806b6720b1) )

	ROM_REGION( 0x10000, "cassette", 0 )      /* (max) 64k for cassette image */
	ROM_LOAD( "dt-1255-b-0.cas", 0x0000, 0x7400, CRC(eb985257) SHA1(1285724352a59c96cc4edf4f43e89dd6d8c585b2) )
ROM_END

ROM_START( cfishing )
	DECOCASS_BIOS_AN_ROMS

	ROM_REGION( 0x01000, "dongle", 0 )    /* dongle data */
	ROM_LOAD( "dp-1250-a-0.dgl", 0x0000, 0x1000, CRC(92a3b387) SHA1(e17a155d02e9ed806590b23a845dc7806b6720b1) )

	ROM_REGION( 0x10000, "cassette", 0 )      /* (max) 64k for cassette image */
	ROM_LOAD( "dt-1250-a-0.cas", 0x0000, 0x7500, CRC(d4a16425) SHA1(25afaabdc8b2217d5e73606a36ea9ba408d7bc4b) )
ROM_END


/* 26 Hamburger (Japan), Burger Time (World) */
/* Photo of Dongle shows DP-126B with Cassette DT-1267B (the "7B" is in a separate white box then the DP-126 label) */
ROM_START( cbtime ) // version 7-B-0
	DECOCASS_BIOS_BN_ROMS

	ROM_REGION( 0x01000, "dongle", 0 )    /* dongle data */
	ROM_LOAD( "dp-126_b.dgl",    0x0000, 0x1000, CRC(25bec0f0) SHA1(9fb1f9699f37937421e26d4fb8fdbcd21a5ddc5c) )

	ROM_REGION( 0x10000, "cassette", 0 )      /* (max) 64k for cassette image */
	ROM_LOAD( "dt-126_7b.cas",   0x0000, 0x8000, CRC(56d7dc58) SHA1(34b2513c9ca7ab40f532b6d6d911aa3012113632) )
ROM_END

ROM_START( chamburger ) // version 0-A-0
	DECOCASS_BIOS_AN_ROMS

	ROM_REGION( 0x01000, "dongle", 0 )    /* dongle data */
	ROM_LOAD( "dp-126_a.dgl",    0x0000, 0x1000, CRC(25bec0f0) SHA1(9fb1f9699f37937421e26d4fb8fdbcd21a5ddc5c) )

	ROM_REGION( 0x10000, "cassette", 0 )      /* (max) 64k for cassette image */
	ROM_LOAD( "dt-126_a.cas",    0x0000, 0x8000, CRC(334fb987) SHA1(c55906bf6059686dd8a587dabbe3fb4d59200ab9) )
ROM_END

/* 27 Burnin' Rubber (Japan/World), Bump'n'Jump (World) */
/* Photo of Dongle shows DP-127B with Cassette DP-1275B (the "5B" is in a separate white box then the DP-127 label) */
ROM_START( cburnrub )
	DECOCASS_BIOS_BN_ROMS

	ROM_REGION( 0x01000, "dongle", 0 )    /* dongle data */
	ROM_LOAD( "dp-127_b.pro",    0x0000, 0x1000, CRC(9f396832) SHA1(0e302fd094474ac792882948a018c73ce76e0759) )

	ROM_REGION( 0x10000, "cassette", 0 )      /* (max) 64k for cassette image */
	ROM_LOAD( "cburnrub.cas",    0x0000, 0x8000, CRC(4528ac22) SHA1(dc0fcc5e5fd21c1c858a90f43c175e36a24b3c3d) ) /* Is this the 5B version? */
ROM_END

ROM_START( cburnrub2 )
	DECOCASS_BIOS_BN_ROMS

	ROM_REGION( 0x01000, "dongle", 0 )    /* dongle data */
	ROM_LOAD( "dp-127_b.pro",    0x0000, 0x1000, CRC(9f396832) SHA1(0e302fd094474ac792882948a018c73ce76e0759) )

	ROM_REGION( 0x10000, "cassette", 0 )      /* (max) 64k for cassette image */
	ROM_LOAD( "cburnrb2.cas",    0x0000, 0x8000, CRC(84a9ed66) SHA1(a9c536e46b89fc6b9c6271776292fed1241d2f3f) ) /* Is this the 5B version? */
ROM_END

ROM_START( cbnj )
	DECOCASS_BIOS_BN_ROMS

	ROM_REGION( 0x01000, "dongle", 0 )    /* dongle data */
	ROM_LOAD( "dp-127_b.pro",    0x0000, 0x1000, CRC(9f396832) SHA1(0e302fd094474ac792882948a018c73ce76e0759) )

	ROM_REGION( 0x10000, "cassette", 0 )      /* (max) 64k for cassette image */
	ROM_LOAD( "cbnj.cas",        0x0000, 0x8000, CRC(eed41560) SHA1(85d5df76efac33cd10427f659c4259afabb3daaf) )
ROM_END

/* 28 Graplop (Japan/World), Cluster Buster (World) */
ROM_START( cgraplop )
	DECOCASS_BIOS_BN_ROMS

	ROM_REGION( 0x01000, "dongle", 0 )    /* dongle data */
	ROM_LOAD( "cgraplop.pro",    0x0000, 0x1000, CRC(ee93787d) SHA1(0c753d62fdce2fdbd5b329a5aa259a967d07a651) )

	ROM_REGION( 0x10000, "cassette", 0 )      /* (max) 64k for cassette image */
	ROM_LOAD( "cgraplop.cas",    0x0000, 0x8000, CRC(d2c1c1bb) SHA1(db67304caa11540363735e7d4bf03507ccbe9980) )
ROM_END

ROM_START( cgraplop2 )
	DECOCASS_BIOS_BN_ROMS

	ROM_REGION( 0x01000, "dongle", 0 )    /* dongle data */
	ROM_LOAD( "cgraplop.pro",    0x0000, 0x1000, CRC(ee93787d) SHA1(0c753d62fdce2fdbd5b329a5aa259a967d07a651) ) /* is this right for this set? */

	ROM_REGION( 0x10000, "cassette", 0 )      /* (max) 64k for cassette image */
	ROM_LOAD( "cgraplop2.cas",   0x0000, 0x8000, CRC(2e728981) SHA1(83ba90d95858d647315a1c311b8643672afea5f7) )
ROM_END

/* 29 La-Pa-Pa (Japan), Rootin' Tootin' (World) */
ROM_START( clapapa )
	DECOCASS_BIOS_BN_ROMS

	ROM_REGION( 0x01000, "dongle", 0 )    /* dongle data */
	ROM_LOAD( "clapapa.pro",     0x0000, 0x1000, CRC(e172819a) SHA1(3492775f4f0a0b31ce5a1a998076829b3f264e98) )

	ROM_REGION( 0x10000, "cassette", 0 )      /* (max) 64k for cassette image */
	ROM_LOAD( "clapapa.cas",     0x0000, 0x8000, CRC(4ffbac24) SHA1(1ec0d7ac1886d4b430dc12be27f387e9d952d235) )
ROM_END

ROM_START( clapapa2 )
	DECOCASS_BIOS_BN_ROMS

	ROM_REGION( 0x01000, "dongle", 0 )   /* dongle data */
	ROM_LOAD( "clapapa.pro",     0x0000, 0x1000, CRC(e172819a) SHA1(3492775f4f0a0b31ce5a1a998076829b3f264e98) )

	ROM_REGION( 0x10000, "cassette", 0 )   /* (max) 64k for cassette image */
	ROM_LOAD( "clapapa2.cas",    0x0000, 0x8000, CRC(069dd3c4) SHA1(5a19392c7ac5aea979187c96267e73bf5126307e) )
ROM_END

/* 30 Skater (Japan), Skater Gaiter (World) */
ROM_START( cskater )
	DECOCASS_BIOS_AN_ROMS

	ROM_REGION( 0x01000, "dongle", 0 )    /* dongle data */
	ROM_LOAD( "dp-130_a.dgl",    0x0000, 0x1000,  CRC(469e80a8) SHA1(f581cd534ce6faba010c6616538cdf9d96d787da) )

	ROM_REGION( 0x10000, "cassette", 0 )      /* (max) 64k for cassette image */
	ROM_LOAD( "dt-130_a.cas",    0x0000, 0x8000,  CRC(1722e5e1) SHA1(e94066ead608df85d3f7310d4a81ba291da4bee6) )
ROM_END

/* 31 Pro Bowling (World) */
ROM_START( cprobowl )
	DECOCASS_BIOS_BN_ROMS

	ROM_REGION( 0x01000, "dongle", 0 )    /* dongle data */
	ROM_LOAD( "cprobowl.pro",    0x0000, 0x1000, CRC(e3a88e60) SHA1(e6e9a2e5ab26e0463c63201a15f7d5a429ec836e) )

	ROM_REGION( 0x10000, "cassette", 0 )      /* (max) 64k for cassette image */
	ROM_LOAD( "cprobowl.cas",    0x0000, 0x8000, CRC(cb86c5e1) SHA1(66c467418cff2ed6d7c121a8b1650ee97ae48fe9) )
ROM_END

/* 32 Night Star (World) */
ROM_START( cnightst )
	DECOCASS_BIOS_BN_ROMS

	ROM_REGION( 0x01000, "dongle", 0 )    /* dongle data */
	ROM_LOAD( "cnightst.pro",    0x0000, 0x1000, CRC(553b0fbc) SHA1(2cdf4560992b62e59b6de760d7996be4ed25f505) )

	ROM_REGION( 0x10000, "cassette", 0 )      /* (max) 64k for cassette image */
	ROM_LOAD( "cnightst.cas",    0x0000, 0x8000, CRC(c6f844cb) SHA1(5fc6154c20ee4e2f4049a78df6f3cacbb96b0dc0) )
ROM_END

ROM_START( cnightst2 )
	DECOCASS_BIOS_BN_ROMS

	ROM_REGION( 0x01000, "dongle", 0 )   /* dongle data */
	ROM_LOAD( "cnightst.pro",    0x0000, 0x1000, CRC(553b0fbc) SHA1(2cdf4560992b62e59b6de760d7996be4ed25f505) )

	ROM_REGION( 0x10000, "cassette", 0 )   /* (max) 64k for cassette image */
	ROM_LOAD( "cnights2.cas",    0x0000, 0x8000, CRC(1a28128c) SHA1(4b620a1919d02814f734aba995115c09dc2db930) )
ROM_END

/* 33 Pro Soccer (World) */
ROM_START( cpsoccer )
	DECOCASS_BIOS_BN_ROMS

	ROM_REGION( 0x01000, "dongle", 0 )    /* dongle data */
	ROM_LOAD( "cprosocc.pro",    0x0000, 0x1000,  CRC(919fabb2) SHA1(3d6a0676cea7b0be0fe69d06e04ca08c36b2851a) )

	ROM_REGION( 0x10000, "cassette", 0 )      /* (max) 64k for cassette image */
	ROM_LOAD( "cprosocc.cas",    0x0000, 0x10000, CRC(76b1ad2c) SHA1(6188667e5bc001dfdf83deaf7251eae794de4702) )
ROM_END

ROM_START( cpsoccerj )
	DECOCASS_BIOS_AN_ROMS

	ROM_REGION( 0x01000, "dongle", 0 )    /* dongle data */
	ROM_LOAD( "dp-133_a.dgl",    0x0000, 0x1000,  CRC(919fabb2) SHA1(3d6a0676cea7b0be0fe69d06e04ca08c36b2851a) )

	ROM_REGION( 0x10000, "cassette", 0 )      /* (max) 64k for cassette image */
	ROM_LOAD( "dt-133_a.cas",    0x0000, 0x10000, CRC(de682a29) SHA1(2ee0dd8cb7fb595020d730a9da5d9cccda3f1264) )
ROM_END

/* 34 Super Doubles Tennis (World) */
ROM_START( csdtenis )
	DECOCASS_BIOS_AN_ROMS

	ROM_REGION( 0x01000, "dongle", 0 )    /* dongle data */
	ROM_LOAD( "dp-134_a.dgl",    0x0000, 0x1000,  CRC(e484d2f5) SHA1(ee4e4c221933d391aeed8ff7182fa931a4e01466) )

	ROM_REGION( 0x10000, "cassette", 0 )      /* (max) 64k for cassette image */
	ROM_LOAD( "dt-134_a.cas",    0x0000, 0x10000, CRC(9a69d961) SHA1(f88e267815ca0697708aca0ac9fa6f7664a0519c) )
ROM_END

/* 37 Zeroize (World) */
ROM_START( czeroize )
	DECOCASS_BIOS_BN_ROMS

	ROM_REGION( 0x01000, "dongle", 0 )    /* dongle data */
	ROM_LOAD( "czeroize.pro",    0x0000, 0x1000, NO_DUMP ) /* The Following have unknown Dongles (dongle data not read) */

	ROM_REGION( 0x10000, "cassette", 0 )      /* (max) 64k for cassette image */
	ROM_LOAD( "czeroize.cas",    0x0000, 0x10000, CRC(3ef0a406) SHA1(645b34cd477e0bb5539c8fe937a7a2dbd8369003) )
ROM_END

/* 39 Ice Cream (Japan), Peter Pepper's Ice Cream Factory (World) */
ROM_START( cppicf )
	DECOCASS_BIOS_BN_ROMS

	ROM_REGION( 0x01000, "dongle", 0 )    /* dongle data */
	ROM_LOAD( "cppicf.pro",      0x0000, 0x1000, CRC(0b1a1ecb) SHA1(2106da6837c78812c102b0eaaa1127fcc21ea780) )

	ROM_REGION( 0x10000, "cassette", 0 )      /* (max) 64k for cassette image */
	ROM_LOAD( "cppicf.cas",      0x0000, 0x8000, CRC(8c02f160) SHA1(03430dd8d4b2e6ca931986dac4d39be6965ffa6f) )
ROM_END

ROM_START( cppicf2 )
	DECOCASS_BIOS_BN_ROMS

	ROM_REGION( 0x01000, "dongle", 0 )   /* dongle data */
	ROM_LOAD( "cppicf.pro",      0x0000, 0x1000, CRC(0b1a1ecb) SHA1(2106da6837c78812c102b0eaaa1127fcc21ea780) )

	ROM_REGION( 0x10000, "cassette", 0 )   /* (max) 64k for cassette image */
	ROM_LOAD( "cppicf2.cas",     0x0000, 0x8000, CRC(78ffa1bc) SHA1(d15f2a240ae7b45885d32b5f507243f82e820d4b) )
ROM_END

/* 40 Fighting Ice Hockey (World) */
ROM_START( cfghtice )
	DECOCASS_BIOS_BN_ROMS

	ROM_REGION( 0x01000, "dongle", 0 )    /* dongle data */
	ROM_LOAD( "cfghtice.pro",    0x0000, 0x1000, CRC(5abd27b5) SHA1(2ab1c171adffd491759036d6ce2433706654aad2) )

	ROM_REGION( 0x10000, "cassette", 0 )      /* (max) 64k for cassette image */
	ROM_LOAD( "cfghtice.cas",    0x0000, 0x10000, CRC(906dd7fb) SHA1(894a7970d5476ed035edd15656e5cf10d6ddcf57) )
ROM_END

/* The Following use Dongle Type 4 (unknown part number?)
    (dongle data is used for most of the graphics) */

/* 38 Scrum Try (World) */
ROM_START( cscrtry )
	DECOCASS_BIOS_BN_ROMS

	ROM_REGION( 0x08000, "dongle", 0 )    /* dongle data */
	ROM_LOAD( "cscrtry.pro",     0x0000, 0x8000, CRC(7bc3460b) SHA1(7c5668ff9a5073e27f4a83b02d79892eb4df6b92) )

	ROM_REGION( 0x10000, "cassette", 0 )      /* (max) 64k for cassette image */
	ROM_LOAD( "cscrtry.cas",     0x0000, 0x8000, CRC(5625f0ca) SHA1(f4b0a6f2ca908880386838f06b626479b4b74134) )
ROM_END

ROM_START( cscrtry2 )
	DECOCASS_BIOS_BN_ROMS

	ROM_REGION( 0x08000, "dongle", 0 )   /* dongle data */
	ROM_LOAD( "cscrtry.pro",     0x0000, 0x8000, CRC(7bc3460b) SHA1(7c5668ff9a5073e27f4a83b02d79892eb4df6b92) )

	ROM_REGION( 0x10000, "cassette", 0 )   /* (max) 64k for cassette image */
	ROM_LOAD( "cscrtry2.cas",    0x0000, 0x8000, CRC(04597842) SHA1(7f1fc3e06b61df880debe9056bdfbbb8600af739) )
ROM_END

/* 41 Oozumou (Japan), The Grand Sumo (World) */
ROM_START( coozumou )
	DECOCASS_BIOS_AN_ROMS

	ROM_REGION( 0x08000, "dongle", 0 )    /* dongle data */
	ROM_LOAD( "dp-141_a.dgl",    0x0000, 0x8000,  CRC(bc379d2c) SHA1(bab19dcb6d68fdbd547ebab1598353f436321157) )

	ROM_REGION( 0x10000, "cassette", 0 )      /* (max) 64k for cassette image */
	ROM_LOAD( "dt-141_1a.cas",   0x0000, 0x10000, CRC(20c2e86a) SHA1(a18248ba00b847a09df0bea7752a21162af8af76) )
ROM_END

/* 44 Boulder Dash (World) */
ROM_START( cbdash )
	DECOCASS_BIOS_BN_ROMS

/*  ROM_REGION( 0x01000, "dongle", 0 ) */ /* (max) 4k for dongle data */
	/* no proms */

	ROM_REGION( 0x10000, "cassette", 0 )      /* (max) 64k for cassette image */
	ROM_LOAD( "cbdash.cas",      0x0000, 0x8000, CRC(cba4c1af) SHA1(5d163d8e31c58b20679c6be06b1aa02df621822b) )
ROM_END

/* The Following have no Dongles at all */

/* 35 Flying Ball (World) */
ROM_START( cflyball )
	DECOCASS_BIOS_BN_ROMS

	/* no dongle data */

	ROM_REGION( 0x10000, "cassette", 0 )      /* (max) 64k for cassette image */
	ROM_LOAD( "cflyball.cas",    0x0000, 0x10000, CRC(cb40d043) SHA1(57698bac7e0d552167efa99d08116bf19a3b29c9) )
ROM_END


DRIVER_INIT_MEMBER(decocass_state,decocass)
{
	/* Call the state save setup code in machine/decocass.c */
	decocass_machine_state_save_init();
	/* and in video/decocass.c, too */
	decocass_video_state_save_init();
}

DRIVER_INIT_MEMBER(decocass_state,decocrom)
{
	/* standard init */
	DRIVER_INIT_CALL(decocass);

	/* convert charram to a banked ROM */
	m_maincpu->space(AS_PROGRAM).install_read_bank(0x6000, 0xafff, "bank1");
	m_maincpu->space(AS_PROGRAM).install_write_handler(0x6000, 0xafff, write8_delegate(FUNC(decocass_state::decocass_de0091_w),this));
	membank("bank1")->configure_entry(0, m_charram);
	membank("bank1")->configure_entry(1, memregion("user3")->base());
	membank("bank1")->configure_entry(2, memregion("user4")->base());
	membank("bank1")->set_entry(0);

	/* install the bank selector */
	m_maincpu->space(AS_PROGRAM).install_write_handler(0xe900, 0xe900, write8_delegate(FUNC(decocass_state::decocass_e900_w),this));
}

READ8_MEMBER(decocass_state::cdstj2a0_input_r )
{
	UINT8 res;
	static const char *const portnames[2][4] = {
		{"P1_MP0", "P1_MP1", "P1_MP2", "P1_MP3"},
		{"P2_MP0", "P2_MP1", "P2_MP2", "P2_MP3"}         };

	if(offset & 6)
		return decocass_input_r(space,offset);

	res = ioport(portnames[offset & 1][m_mux_data])->read();

	return res;
}

WRITE8_MEMBER(decocass_state::cdstj2a0_mux_w )
{
	m_mux_data = (data & 0xc) >> 2;
	/* bit 0 and 1 are p1/p2 lamps */

	if(data & ~0xf)
		printf("%02x\n",data);
}

DRIVER_INIT_MEMBER(decocass_state,cdstj2a0)
{
	/* standard init */
	DRIVER_INIT_CALL(decocass);

	/* install custom mahjong panel */
	m_maincpu->space(AS_PROGRAM).install_write_handler(0xe413, 0xe413, write8_delegate(FUNC(decocass_state::cdstj2a0_mux_w), this));
	m_maincpu->space(AS_PROGRAM).install_read_handler(0xe600, 0xe6ff, read8_delegate(FUNC(decocass_state::cdstj2a0_input_r), this));
}

/* -- */ GAME( 1981, decocass,  0,        decocass, decocass, decocass_state, decocass, ROT270, "Data East Corporation", "DECO Cassette System", MACHINE_IS_BIOS_ROOT )
/* -- */ GAME( 1981, ctsttape,  decocass, ctsttape, decocass, decocass_state, decocass, ROT270, "Data East Corporation", "Test Tape (DECO Cassette)", 0 )
/* 01 */ GAME( 1980, chwych0a,  decocass, chwych0a, chwych0a, decocass_state, decocass, ROT270, "Data East Corporation", "HWY Chase [DECO Cassette MD] (No.01/Ver.0,Japan)", MACHINE_IMPERFECT_GRAPHICS ) /* headlight not fixed */
/*    */ GAME( 1980, chwych1b,  chwych0a, chwych0a, chwych0a, decocass_state, decocass, ROT270, "Data East Corporation", "HWY Chase [DECO Cassette MD] (No.01/Ver.1,US)", MACHINE_IMPERFECT_GRAPHICS ) /* headlight not fixed */
/* 02 */ GAME( 1980, cninjt0a,  decocass, cninjt0a, cninjt0a, decocass_state, decocass, ROT270, "Data East Corporation", "Sengoku Ninjatai [DECO Cassette MD] (No.02/Ver.0,Japan)", MACHINE_NOT_WORKING ) /* background layer not fixed */
/*    */ GAME( 1980, cninjt1d,  cninjt0a, cninjt0a, cninjt0a, decocass_state, decocass, ROT270, "Data East Corporation", "Sengoku Ninjatai [DECO Cassette MD] (No.02/Ver.1,Europe)", MACHINE_NOT_WORKING ) /* background layer not fixed */
/* 03 */ GAME( 1980, cmanht0a,  decocass, cmanht0a, cmanht0a, decocass_state, decocass, ROT270, "Data East Corporation", "Manhattan [DECO Cassette MD] (No.03/Ver.0,Japan)", 0 )
/*    */ GAME( 1980, cmanht1a,  cmanht0a, cmanht0a, cmanht0a, decocass_state, decocass, ROT270, "Data East Corporation", "Manhattan [DECO Cassette MD] (No.03/Ver.1,Japan)", 0 )
/*    */ GAME( 1980, cmanht2a,  cmanht0a, cmanht0a, cmanht0a, decocass_state, decocass, ROT270, "Data East Corporation", "Manhattan [DECO Cassette MD] (No.03/Ver.2,Japan)", 0 )
/* 04 */ GAME( 1981, cterra2a,  decocass, cterra2a, cterra2a, decocass_state, decocass, ROT270, "Data East Corporation", "Terranean [DECO Cassette MD] (No.04/Ver.2,Japan)", 0 )
/*    */ GAME( 1981, cterra2d,  cterra2a, cterra2a, cterra2a, decocass_state, decocass, ROT270, "Data East Corporation", "Terranean [DECO Cassette MD] (No.04/Ver.2,Europe)", 0 )
/*    */ GAME( 1981, cterra4a,  cterra2a, cterra2a, cterra2a, decocass_state, decocass, ROT270, "Data East Corporation", "Terranean [DECO Cassette MD] (No.04/Ver.4,Japan)", 0 )
/*    */ GAME( 1981, cterra4b,  cterra2a, cterra2a, cterra2a, decocass_state, decocass, ROT270, "Data East Corporation", "Terranean [DECO Cassette MD] (No.04/Ver.4,US)", 0 )
/* 05 */ // 1981 Missile Splinter (canceled/unreleased)
/* 06 */ GAME( 1980, cnebul2a,  decocass, cnebul2a, cnebul2a, decocass_state, decocass, ROT270, "Data East Corporation", "Nebula [DECO Cassette MD] (No.06/Ver.2,Japan)", 0 )
/* 07 */ GAME( 1981, castro6a,  decocass, castro6a, castro6a, decocass_state, decocass, ROT270, "Data East Corporation", "Astro Fantasia [DECO Cassette MD] (No.07/Ver.6,Japan)", MACHINE_IMPERFECT_GRAPHICS ) /* shoot invisible at boss scene */
/*    */ GAME( 1981, castro6b,  castro6a, castro6a, castro6a, decocass_state, decocass, ROT270, "Data East Corporation", "Astro Fantasia [DECO Cassette MD] (No.07/Ver.6,US)", MACHINE_IMPERFECT_GRAPHICS ) /* shoot invisible at boss scene */
/*    */ GAME( 1981, castro7a,  castro6a, castro6a, castro6a, decocass_state, decocass, ROT270, "Data East Corporation", "Astro Fantasia [DECO Cassette MD] (No.07/Ver.7,Japan)", MACHINE_IMPERFECT_GRAPHICS ) /* shoot invisible at boss scene */
/*    */ GAME( 1981, castro7b,  castro6a, castro6a, castro6a, decocass_state, decocass, ROT270, "Data East Corporation", "Astro Fantasia [DECO Cassette MD] (No.07/Ver.7,US)", MACHINE_IMPERFECT_GRAPHICS ) /* shoot invisible at boss scene */
/*    */ GAME( 1981, castro4c,  castro6a, castro6a, castro6a, decocass_state, decocass, ROT270, "Data East Corporation", "Astro Fantasia [DECO Cassette MD] (No.07/Ver.4,UK)", MACHINE_IMPERFECT_GRAPHICS ) /* shoot invisible at boss scene */
/* 08 */ GAME( 1981, ctower0a,  decocass, ctower0a, ctower0a, decocass_state, decocass, ROT270, "Data East Corporation", "The Tower [DECO Cassette MD] (No.08/Ver.0,Japan)", MACHINE_NOT_WORKING ) /* second lever not implemented, correct dip unknown */
/* 09 */ GAME( 1981, csastf4a,  decocass, csastf4a, csastf4a, decocass_state, decocass, ROT270, "Data East Corporation", "Super Astro Fighter [DECO Cassette MD] (No.09/Ver.4,Japan)", 0 )
/*    */ GAME( 1981, csastf4b,  csastf4a, csastf4a, csastf4a, decocass_state, decocass, ROT270, "Data East Corporation", "Super Astro Fighter [DECO Cassette MD] (No.09/Ver.4,US)", 0 )
/*    */ GAME( 1981, csastf2c,  csastf4a, csastf4a, csastf4a, decocass_state, decocass, ROT270, "Data East Corporation", "Super Astro Fighter [DECO Cassette MD] (No.09/Ver.2,UK)", 0 )
/*    */ GAME( 1981, csastf3b,  csastf4a, csastf4a, csastf4a, decocass_state, decocass, ROT270, "Data East Corporation", "Super Astro Fighter [DECO Cassette MD] (No.09/Ver.3,US)", 0 )
/* 10 */ // 1981.?? Ocean to Ocean (medal)
/* 11 */ GAME( 1981, clocknch,  decocass, clocknch, clocknch, decocass_state, decocass, ROT270, "Data East Corporation", "Lock'n'Chase (DECO Cassette)", 0 )
/* 12 */ // 1981.08 Flash Boy/DECO Kid
/* 13 */ GAME( 1981, cpgolf1a,  decocass, cpgolf1a, cpgolf1a, decocass_state, decocass, ROT270, "Data East Corporation", "Pro Golf [DECO Cassette MD] (No.13/Ver.1,Japan)", 0 )
/*    */ GAME( 1981, cpgolf1b,  cpgolf1a, cpgolf1a, cpgolf1a, decocass_state, decocass, ROT270, "Data East Corporation", "Pro Golf [DECO Cassette MD] (No.13/Ver.1,US)", 0 )
/*    */ GAME( 1981, cpgolf4a,  cpgolf1a, cpgolf1a, cpgolf4a, decocass_state, decocass, ROT270, "Data East Corporation", "18 Challenge Pro Golf [DECO Cassette MD] (No.13/Ver.4,Japan)", 0 )
/*    */ GAME( 1981, cpgolf6a,  cpgolf1a, cpgolf1a, cpgolf6a, decocass_state, decocass, ROT270, "Data East Corporation", "Tournament Pro Golf [DECO Cassette MD] (No.13/Ver.6,Japan)", 0 )
/*    */ GAME( 1981, cpgolf7d,  cpgolf1a, cpgolf1a, cpgolf6a, decocass_state, decocass, ROT270, "Data East Corporation", "Tournament Pro Golf [DECO Cassette MD] (No.13/Ver.7,Europe)", 0 )
/*    */ GAME( 1981, cpgolf9b,  cpgolf1a, cpgolf1a, cpgolf9b, decocass_state, decocass, ROT270, "Data East Corporation", "Tournament Pro Golf [DECO Cassette MD] (No.13/Ver.9,US)", 0 )
/* 14 */ GAME( 1981, cdstj2a0,  decocass, cdstj2a0, cdstj2a0, decocass_state, cdstj2a0, ROT270, "Data East Corporation", "DS Telejan [DECO Cassette MD] (No.14/Ver.2,Japan)", 0 )
/*    */ GAME( 1981, cdstj3a0,  cdstj2a0, cdstj2a0, cdstj2a0, decocass_state, cdstj2a0, ROT270, "Data East Corporation", "DS Telejan [DECO Cassette MD] (No.14/Ver.3,Japan)", 0 )
/*    */ GAME( 1981, cdstj4a2,  cdstj2a0, cdstj2a0, cdstj2a0, decocass_state, cdstj2a0, ROT270, "Data East Corporation", "DS Telejan [DECO Cassette MD] (No.14/Ver.4/Rev.2,Japan)", 0 )
/*    */ GAME( 1981, cdstj4a3,  cdstj2a0, cdstj2a0, cdstj2a0, decocass_state, cdstj2a0, ROT270, "Data East Corporation", "DS Telejan [DECO Cassette MD] (No.14/Ver.4/Rev.3,Japan)", 0 )
/* 15 */ GAME( 1981, cluckypo,  decocass, cluckypo, decocass, decocass_state, decocass, ROT270, "Data East Corporation", "Lucky Poker (DECO Cassette)", 0 )
/* 16 */ GAME( 1981, ctisland,  decocass, ctisland, decocass, decocass_state, decocrom, ROT270, "Data East Corporation", "Treasure Island (DECO Cassette, set 1)", 0 )
/*    */ GAME( 1981, ctisland2, ctisland, ctisland, decocass, decocass_state, decocrom, ROT270, "Data East Corporation", "Treasure Island (DECO Cassette, set 2)", 0 )
/*    */ GAME( 1981, ctisland3, ctisland, ctisland, decocass, decocass_state, decocrom, ROT270, "Data East Corporation", "Treasure Island (DECO Cassette, set 3)", MACHINE_NOT_WORKING )
/* 17 */ // 1981.10 Bobbitto
/* 18 */ GAME( 1982, cexplr0a,  decocass, cexplr0a, cexplr0a, decocass_state, decocrom, ROT270, "Data East Corporation", "Explorer [DECO Cassette MD] (No.18/Ver.0,Japan)", MACHINE_IMPERFECT_GRAPHICS ) /* graphic roms need redump ? */
/*    */ GAME( 1982, cexplr0b,  cexplr0a, cexplr0a, cexplr0a, decocass_state, decocrom, ROT270, "Data East Corporation", "Explorer [DECO Cassette MD] (No.18/Ver.0,US)", MACHINE_IMPERFECT_GRAPHICS ) /* graphic roms need redump ? */
/* 19 */ GAME( 1982, cdiscon1,  decocass, cdiscon1, decocass, decocass_state, decocass, ROT270, "Data East Corporation", "Disco No.1 (DECO Cassette)", 0 )
/*    */ GAME( 1982, csweetht,  cdiscon1, cdiscon1, decocass, decocass_state, decocass, ROT270, "Data East Corporation", "Sweet Heart (DECO Cassette)", 0 )
/* 20 */ GAME( 1982, ctornado,  decocass, ctornado, ctornado, decocass_state, decocass, ROT270, "Data East Corporation", "Tornado (DECO Cassette)", 0 )
/* 21 */ GAME( 1982, cmissnx,   decocass, cmissnx,  cmissnx,  decocass_state, decocass, ROT270, "Data East Corporation", "Mission-X (DECO Cassette)", 0 )
/* 22 */ GAME( 1982, cptennis,  decocass, cptennis, decocass, decocass_state, decocass, ROT270, "Data East Corporation", "Pro Tennis (DECO Cassette)", 0 )
/* 23 */ // ?
/* 24 */ // 1982.07 Tsumego Kaisyou
/* 25 */ GAME( 1982, cadanglr,  decocass, cfishing, cfishing, decocass_state, decocass, ROT270, "Data East Corporation", "Angler Dangler (DECO Cassette)", 0 )
/* 25 */ GAME( 1982, cfishing,  cadanglr, cfishing, cfishing, decocass_state, decocass, ROT270, "Data East Corporation", "Fishing (DECO Cassette, Japan)", 0 )
/* 26 */ GAME( 1983, cbtime,    decocass, cbtime,   cbtime,   decocass_state, decocass, ROT270, "Data East Corporation", "Burger Time (DECO Cassette)", 0 )
/*    */ GAME( 1982, chamburger,cbtime,   cbtime,   cbtime,   decocass_state, decocass, ROT270, "Data East Corporation", "Hamburger (DECO Cassette, Japan)", 0 )
/* 27 */ GAME( 1982, cburnrub,  decocass, cburnrub, decocass, decocass_state, decocass, ROT270, "Data East Corporation", "Burnin' Rubber (DECO Cassette, set 1)", 0 )
/*    */ GAME( 1982, cburnrub2, cburnrub, cburnrub, decocass, decocass_state, decocass, ROT270, "Data East Corporation", "Burnin' Rubber (DECO Cassette, set 2)", 0 )
/*    */ GAME( 1982, cbnj,      cburnrub, cburnrub, decocass, decocass_state, decocass, ROT270, "Data East Corporation", "Bump 'n' Jump (DECO Cassette, Japan)", 0 )
/* 28 */ GAME( 1983, cgraplop,  decocass, cgraplop, cgraplop, decocass_state, decocass, ROT270, "Data East Corporation", "Cluster Buster (DECO Cassette)", 0 )
/*    */ GAME( 1983, cgraplop2, cgraplop, cgraplop2,cgraplop, decocass_state, decocass, ROT270, "Data East Corporation", "Graplop (no title screen) (DECO Cassette)", 0 ) // a version with title screen exists, see reference videos
/* 29 */ GAME( 1983, clapapa,   decocass, clapapa,  decocass, decocass_state, decocass, ROT270, "Data East Corporation", "Rootin' Tootin' / La-Pa-Pa (DECO Cassette)" , 0) /* Displays 'La-Pa-Pa during attract */
/*    */ GAME( 1983, clapapa2,  clapapa,  clapapa,  decocass, decocass_state, decocass, ROT270, "Data East Corporation", "Rootin' Tootin' (DECO Cassette)" , 0) /* Displays 'Rootin' Tootin' during attract */
/* 30 */ GAME( 1983, cskater,   decocass, cskater,  cskater,  decocass_state, decocass, ROT270, "Data East Corporation", "Skater (DECO Cassette, Japan)", 0 )
/* 31 */ GAME( 1983, cprobowl,  decocass, cprobowl, decocass, decocass_state, decocass, ROT270, "Data East Corporation", "Pro Bowling (DECO Cassette)", 0 )
/* 32 */ GAME( 1983, cnightst,  decocass, cnightst, cnightst, decocass_state, decocass, ROT270, "Data East Corporation", "Night Star (DECO Cassette, set 1)", 0 )
/*    */ GAME( 1983, cnightst2, cnightst, cnightst, cnightst, decocass_state, decocass, ROT270, "Data East Corporation", "Night Star (DECO Cassette, set 2)", 0 )
/* 33 */ GAME( 1983, cpsoccer,  decocass, cpsoccer, cpsoccer, decocass_state, decocass, ROT270, "Data East Corporation", "Pro Soccer (DECO Cassette)", 0 )
/*    */ GAME( 1983, cpsoccerj, cpsoccer, cpsoccer, cpsoccer, decocass_state, decocass, ROT270, "Data East Corporation", "Pro Soccer (DECO Cassette, Japan)", 0 )
/* 34 */ GAME( 1983, csdtenis,  decocass, csdtenis, csdtenis, decocass_state, decocass, ROT270, "Data East Corporation", "Super Doubles Tennis (DECO Cassette, Japan)", MACHINE_WRONG_COLORS )
/* 35 */ GAME( 1985, cflyball,  decocass, cflyball, decocass, decocass_state, decocass, ROT270, "Data East Corporation", "Flying Ball (DECO Cassette)", 0 )
/* 36 */ // ?
/* 37 */ GAME( 1983, czeroize,  decocass, czeroize, decocass, decocass_state, decocass, ROT270, "Data East Corporation", "Zeroize (DECO Cassette)", 0 )
/* 38 */ GAME( 1984, cscrtry,   decocass, type4,    cscrtry,  decocass_state, decocass, ROT270, "Data East Corporation", "Scrum Try (DECO Cassette, set 1)", 0 )
/*    */ GAME( 1984, cscrtry2,  cscrtry,  type4,    cscrtry,  decocass_state, decocass, ROT270, "Data East Corporation", "Scrum Try (DECO Cassette, set 2)", 0 )
/* 39 */ GAME( 1984, cppicf,    decocass, cppicf,   decocass, decocass_state, decocass, ROT270, "Data East Corporation", "Peter Pepper's Ice Cream Factory (DECO Cassette, set 1)", 0 )
/*    */ GAME( 1984, cppicf2,   cppicf,   cppicf,   decocass, decocass_state, decocass, ROT270, "Data East Corporation", "Peter Pepper's Ice Cream Factory (DECO Cassette, set 2)", 0 )
/* 40 */ GAME( 1984, cfghtice,  decocass, cfghtice, cfghtice, decocass_state, decocass, ROT270, "Data East Corporation", "Fighting Ice Hockey (DECO Cassette)", 0 )
/* 41 */ GAME( 1984, coozumou,  decocass, type4,    cscrtry,  decocass_state, decocass, ROT270, "Data East Corporation", "Oozumou - The Grand Sumo (DECO Cassette, Japan)", 0 )
/* 42 */ // 1984.08 Hellow Gateball // not a typo, this is official spelling
/* 43 */ // ?
/* 44 */ GAME( 1985, cbdash,    decocass, cbdash,   cbdash,   decocass_state, decocass, ROT270, "Data East Corporation", "Boulder Dash (DECO Cassette)", 0 )

/* UX7 */ // 1984.12 Tokyo MIE Clinic/Tokyo MIE Shinryoujo
/* UX8 */ // 1985.01 Tokyo MIE Clinic/Tokyo MIE Shinryoujo Part 2 ?
/* UX9 */ // 1985.05 Geinoujin Shikaku Shiken
