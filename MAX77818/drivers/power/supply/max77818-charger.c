/*
 * Copyright (c) 2019 Maxim Integrated Products, Inc.
 * Author: Maxim Integrated <opensource@maximintegrated.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */


#define DEBUG

#define log_level	1

#include <linux/version.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/err.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/mutex.h>
#include <linux/interrupt.h>
#include <linux/delay.h>

/* for Regmap */
#include <linux/regmap.h>

/* for Device Tree */
#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_irq.h>

#include <linux/power_supply.h>
#include <linux/mfd/max77818.h>
#include <linux/power/max77818-charger.h>

#define DRIVER_DESC    "MAX77818 Charger Driver"
#define DRIVER_NAME    MAX77818_CHARGER_NAME
#define DRIVER_VERSION "1.0"
#define DRIVER_AUTHOR  "opensource@maximintegrated.com "

#define M2SH	__CONST_FFS

/* Register map */
#define REG_CHG_INT				0xB0
#define REG_CHG_INT_MASK	0xB1
#define BIT_AICL					BIT(7)
#define BIT_CHGIN					BIT(6)
#define BIT_WCIN					BIT(5)
#define BIT_CHG						BIT(4)
#define BIT_BAT						BIT(3)
#define BIT_BATP					BIT(2)
#define BIT_BYP						BIT(0)

#define REG_CHG_INT_OK				0xB2
#define BIT_AICL_OK					BIT(7)
#define BIT_CHGIN_OK				BIT(6)
#define BIT_WCIN_OK					BIT(5)
#define BIT_CHG_OK					BIT(4)
#define BIT_BAT_OK					BIT(3)
#define BIT_BATP_OK					BIT(2)
#define BIT_BYP_OK					BIT(0)

#define REG_CHG_DTLS_00			0xB3
#define BIT_CHGIN_DTLS			BITS(6, 5)
#define BIT_WCIN_DTLS				BITS(4, 3)
#define BIT_BATP_DTLS				BIT(0)

#define REG_CHG_DTLS_01			0xB4
#define BIT_TREG						BIT(7)
#define BIT_BAT_DTLS				BITS(6, 4)
#define BIT_CHG_DTLS				BITS(3, 0)

#define REG_CHG_DTLS_02				0xB5
#define BIT_BYP_DTLS				BITS(3, 0)
#define BIT_BCKNegILIM				BIT(2)
#define BIT_BSTILIM					BIT(1)
#define BIT_OTGILIM					BIT(0)

#define REG_CHG_CNFG_00				0xB7
#define BIT_OTG_CTRL				BIT(7)
#define BIT_DISIBS					BIT(6)
#define BIT_SPREAD					BIT(5)
#define BIT_WDTEN					BIT(4)
#define BIT_MODE					BITS(3, 0)
#define BIT_MODE_BOOST				BIT(3)
#define BIT_MODE_BUCK				BIT(2)
#define BIT_MODE_OTG				BIT(1)
#define BIT_MODE_CHARGER			BIT(0)

#define REG_CHG_CNFG_01				0xB8
#define BIT_PQEN					BIT(7)
#define BIT_LSEL					BIT(6)
#define BIT_CHG_RSTRT				BITS(5, 4)
#define BIT_FSW						BIT(3)
#define BIT_FCHGTIME				BITS(2, 0)

#define REG_CHG_CNFG_02				0xB9
#define BIT_OTG_ILIM				BITS(7, 6)
#define BIT_CHG_CC					BITS(5, 0)

#define REG_CHG_CNFG_03				0xBA
#define BIT_ILIM					BITS(7, 6)
#define BIT_TO_TIME					BITS(5, 3)
#define BIT_TO_ITH					BITS(2, 0)

#define REG_CHG_CNFG_04				0xBB
#define BIT_MINVSYS					BITS(7, 6)
#define BIT_CHG_CV_PRM				BITS(5, 0)

#define REG_CHG_CNFG_06				0xBD
#define BIT_CHGPROT					BITS(3, 2)
#define BIT_WDTCLR					BITS(1, 0)

#define REG_CHG_CNFG_07				0xBE
#define BIT_REGTEMP					BITS(6, 5)

#define REG_CHG_CNFG_09				0xC0
#define BIT_CHGIN_ILIM				BITS(6, 0)

#define REG_CHG_CNFG_10				0xC1
#define BIT_WCIN_ILIM				BITS(5, 0)

#define REG_CHG_CNFG_11				0xC2
#define BIT_VBYPSET					BITS(6, 0)

#define REG_CHG_CNFG_12				0xC3
#define BIT_WCINSEL					BIT(6)
#define BIT_CHGINSEL				BIT(5)
#define BIT_VCHGIN_REG				BITS(4, 3)
#define BIT_B2SOVRC					BITS(2, 0)

#define __lock(_me)    mutex_lock(&(_me)->lock)
#define __unlock(_me)  mutex_unlock(&(_me)->lock)

/* detail register bit description */
enum {
	WCIN_DTLS_UVLO,
	WCIN_DTLS_INVALID_01,
	WCIN_DTLS_OVLO,
	WCIN_DTLS_VALID,
};

enum {
	CHGIN_DTLS_UVLO,
	CHGIN_DTLS_INVALID_01,
	CHGIN_DTLS_OVLO,
	CHGIN_DTLS_VALID,
};

enum {
	CHG_DTLS_PREQUAL,
	CHG_DTLS_FASTCHARGE_CC,
	CHG_DTLS_FASTCHARGE_CV,
	CHG_DTLS_TOPOFF,
	CHG_DTLS_DONE,
	CHG_DTLS_RESEVRED_05,
	CHG_DTLS_OFF_TIMER_FAULT,
	CHG_DTLS_OFF_SUSPEND,
	CHG_DTLS_OFF_INPUT_INVALID,
	CHG_DTLS_RESERVED_09,
	CHG_DTLS_OFF_JUCTION_TEMP,
	CHG_DTLS_OFF_WDT_EXPIRED,
};

enum {
	BAT_DTLS_NO_BATTERY,
	BAT_DTLS_RESERVED_01,
	BAT_DTLS_TIMER_FAULT,
	BAT_DTLS_OKAY,
	BAT_DTLS_OKAY_LOW,
	BAT_DTLS_OVERVOLTAGE,
	BAT_DTLS_OVERCURRENT,
	BAT_DTLS_RESERVED_07,
};

#define SIOP_INPUT_LIMIT_CURRENT                1200
#define SIOP_CHARGING_LIMIT_CURRENT             1000
#define SLOW_CHARGING_CURRENT_STANDARD          400

#define IRQ_WORK_DELAY              0
static char *chg_supplied_to[] = {
	"max77818-charger",
};

struct max77818_charger_data {
	struct device           *dev;
	struct max77818_dev     *max77818;
	struct regmap						*regmap;
	struct power_supply		*psy_chg;
	struct power_supply_desc		psy_chg_d;
	int						byp_irq;
	int						chgin_irq;
	int						wcin_irq;
	int						aicl_irq;

	int						irq;
	int						irq_mask;
	int						details_0;
	int						details_1;
	int						details_2;
	spinlock_t              irq_lock;
	struct delayed_work		irq_work;

	/* mutex */
	struct mutex			lock;

	int				present;
	int				health;
	int				status;
	int				charge_type;

	struct max77818_charger_platform_data *pdata;

};

static inline struct power_supply *get_power_supply_by_name(char *name)
{
	if (!name)
		return (struct power_supply *)NULL;
	else
		return power_supply_get_by_name(name);
}

#define psy_do_property(name, function, property, value) \
{	\
	struct power_supply *psy;	\
	int ret;	\
	psy = get_power_supply_by_name((name));	\
	if (!psy) {	\
		pr_err("%s: Fail to "#function" psy (%s)\n",	\
				__func__, (name));	\
		value.intval = 0;	\
	} else {	\
	if (psy->function##_property != NULL) { \
		ret = psy->function##_property(psy, (property), &(value)); \
		if (ret < 0) {	\
			pr_err("%s: Fail to "#name" "#function" (%d=>%d)\n", \
				__func__, (property), ret);	\
			value.intval = 0;	\
		}	\
	}	\
	}	\
}

static enum power_supply_property max77818_charger_props[] = {
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_CHARGE_TYPE,
	POWER_SUPPLY_PROP_HEALTH,
	POWER_SUPPLY_PROP_ONLINE,
	POWER_SUPPLY_PROP_CURRENT_MAX,
	POWER_SUPPLY_PROP_CURRENT_NOW,
};

#define GET_TO_ITH(X)	(X < 4 ? (X*25+100) : (X*50))	/* mA */

#define SET_TO_ITH(X)	(X < 100 ? 0x00 :	\
		X < 200 ? (X-100)/25 :	\
		X < 350 ? (X/50) : 0x07)	/* mA */

static void max77818_charger_initialize(struct max77818_charger_data *charger);

/* charger API function */
static int max77818_charger_unlock(struct max77818_charger_data *charger)
{
	int rc;

	rc = regmap_update_bits(charger->regmap, REG_CHG_CNFG_06, BIT_CHGPROT,
			BIT_CHGPROT);

	if (unlikely(IS_ERR_VALUE(rc))) {
		pr_err("%s: failed to unlock [%d]\n", __func__, rc);
		goto out;
	}

out:
	return rc;
}

static bool max77818_charger_present_input(struct max77818_charger_data
	*charger)
{
	u8 chg_int_ok = 0;
	int rc;

	rc = max77818_read(charger->regmap, REG_CHG_INT_OK, &chg_int_ok);
	if (unlikely(IS_ERR_VALUE(rc)))
		return false;

	if ((chg_int_ok & BIT_CHGIN_OK) == BIT_CHGIN_OK)
		return true;
	/* check whether charging or not in the UVLO condition */
	if (((charger->details_0 & BIT_CHGIN_DTLS) == 0) &&
			(((charger->details_1 & BIT_CHG_DTLS) ==
					CHG_DTLS_FASTCHARGE_CC) ||
			 ((charger->details_1 & BIT_CHG_DTLS) ==
					CHG_DTLS_FASTCHARGE_CV))) {
		return true;
	} else {
		return false;
	}
}

static bool max77818_wcharger_present_input(struct max77818_charger_data
	*charger)
{
	u8 chg_int_ok = 0;
	int rc;

	rc = max77818_read(charger->regmap, REG_CHG_INT_OK, &chg_int_ok);
	if (unlikely(IS_ERR_VALUE(rc)))
		return false;

	if ((chg_int_ok & BIT_WCIN_OK) == BIT_WCIN_OK)
		return true;
	/* check whether charging or not in the UVLO condition */
	if (((charger->details_0 & BIT_WCIN_DTLS) == 0) &&
			(((charger->details_1 & BIT_CHG_DTLS) ==
					CHG_DTLS_FASTCHARGE_CC)		||
			 ((charger->details_1 & BIT_CHG_DTLS) ==
					CHG_DTLS_FASTCHARGE_CV))) {
		return true;
	} else
		return false;
}

static int max77818_charger_get_input_current(struct max77818_charger_data
	*charger)
{
	u8 reg_data = 0;
	int steps[3] = { 0, 33, 67 };	/* mA */
	int get_current, quotient, remainder;

	pr_info("max77818 charger get input current");

	if (max77818_charger_present_input(charger) == true) {
		max77818_read(charger->regmap, REG_CHG_CNFG_09, &reg_data);

		quotient = reg_data / 3;
		remainder = reg_data % 3;

		if ((reg_data & BIT_CHGIN_ILIM) < 3)
			get_current = 100;	/* 100mA */
		else if ((reg_data & BIT_CHGIN_ILIM) > 0x78)
			get_current = 4000;	/* 4000mA */
		else
			get_current = quotient*100 + steps[remainder];
	} else if (max77818_wcharger_present_input(charger)) {
		max77818_read(charger->regmap, REG_CHG_CNFG_10, &reg_data);

		if (reg_data <= 3)
			get_current = 60;
		else
			get_current = 60 + (reg_data - 3) * 20;
	} else {
		/* default TA input current */
		max77818_read(charger->regmap, REG_CHG_CNFG_09, &reg_data);

		quotient = reg_data / 3;
		remainder = reg_data % 3;

		if ((reg_data & BIT_CHGIN_ILIM) < 3)
			get_current = 100;	/* 100mA */
		else if ((reg_data & BIT_CHGIN_ILIM) > 0x78)
			get_current = 4000;	/* 4000mA */
		else
			get_current = quotient*100 + steps[remainder];
	}
	return get_current;
}

static int max77818_charger_set_input_current(struct max77818_charger_data
	*charger,
	int input_current)
{
	int quotient, remainder;
	u8 reg_data = 0;

	/* unit mA */
	if (!input_current) {
		reg_data = 0;
	} else {
		quotient = input_current / 100;
		remainder = input_current % 100;

		if (remainder >= 67)
			reg_data |= (quotient * 3) + 2;
		else if (remainder >= 33)
			reg_data |= (quotient * 3) + 1;
		else if (remainder < 33)
			reg_data |= quotient * 3;
	}
	pr_info("%s: reg_data(0x%02x), charging current(%d)\n",
			__func__, reg_data, input_current);

	return regmap_update_bits(charger->regmap, REG_CHG_CNFG_09,
			BIT_CHGIN_ILIM, reg_data);
}

static int max77818_charger_get_charge_current(struct max77818_charger_data
	*charger)
{
	struct regmap *regmap = charger->regmap;

	u8 reg_data = 0;
	int get_current;

	max77818_read(regmap, REG_CHG_CNFG_02, &reg_data);

	if ((reg_data & BIT_CHG_CC) < 2)
		get_current = 100;	/* 100mA */
	else if ((reg_data & BIT_CHG_CC) > 0x3C)
		get_current = 3000; /* 3000mA */
	else
		get_current = (reg_data & BIT_CHG_CC)*50;

	pr_info("%s: reg_data(0x%02x), get_current(%d)\n",
			__func__, reg_data, get_current);

	return get_current;
}

static int max77818_charger_set_charge_current(struct max77818_charger_data
	*charger,
	int fast_charging_current)
{
	int curr_step = 50;
	u8 reg_data = 0;
	int rc;

	/* unit mA */
	if (!fast_charging_current) {
		rc = regmap_update_bits(charger->regmap, REG_CHG_CNFG_02,
				BIT_CHG_CC, 0);

	} else {
		reg_data = (fast_charging_current / curr_step);
		rc = regmap_update_bits(charger->regmap, REG_CHG_CNFG_02,
				BIT_CHG_CC, reg_data);
	}
	pr_info("%s: reg_data(0x%02x), charging current(%d)\n",
			__func__, reg_data, fast_charging_current);

	return rc;

}

static int max77818_charger_set_topoff_current(struct max77818_charger_data
	*charger,
	int termination_current,
	int termination_time)
{
	u8 reg_data;

	/* termination_current (mA) */
	reg_data = SET_TO_ITH(termination_current);

	/* termination_time (min) */
	termination_time = termination_time;
	reg_data |= ((termination_time / 10) << M2SH(BIT_TO_TIME));
	pr_info("%s: reg_data(0x%02x), topoff(%d), time(%d)\n",
			__func__, reg_data, termination_current,
			termination_time);
	return regmap_update_bits(charger->regmap, REG_CHG_CNFG_09,
			BIT_TO_ITH | BIT_TO_TIME, reg_data);

}

static int max77818_charger_set_enable(struct max77818_charger_data
	*charger, int en)
{
	return regmap_update_bits(charger->regmap,
		REG_CHG_CNFG_00, BIT_MODE_CHARGER, !!en);
}


static int max77818_charger_exit_dev(struct max77818_charger_data
	*charger)
{
	struct max77818_charger_platform_data *pdata = charger->pdata;
	int rc;

	rc = max77818_charger_set_enable(charger, false);
	if (unlikely(IS_ERR_VALUE(rc))) {
		pr_err("CHG_CNFG_00 write error [%d]\n", rc);
		return rc;
	}

	rc = max77818_charger_set_charge_current(charger,
		pdata->fast_charge_current);

	return rc;
}

static int max77818_charger_init_dev(struct max77818_charger_data
	*charger)
{
	int rc;

	/* charger enable */
	rc = max77818_charger_set_enable(charger, true);

	return rc;
}

static void max77818_charger_initialize(struct max77818_charger_data
	*charger)
{
	struct max77818_charger_platform_data *pdata = charger->pdata;
	int rc;
	u8 val, temp_val;

	pr_info("%s\n", __func__);

	/* interrupt mask - if you want to enable some bits,
	*	you should clear them
	*/
	val  = 0;
	val |= BIT_AICL;
	val |= BIT_CHGIN;
	val |= BIT_WCIN;
	val |= BIT_CHG;
	val |= BIT_BAT;
	val |= BIT_BATP;
	val |= BIT(1);
	val |= BIT_BYP;

	rc = max77818_write(charger->regmap, REG_CHG_INT_MASK, val);
	if (unlikely(IS_ERR_VALUE(rc))) {
		pr_err("CHG_INT_MASK write error [%d]\n", rc);
		goto out;
	}

	/* unlock charger register */
	rc = max77818_charger_unlock(charger);
	if (unlikely(IS_ERR_VALUE(rc)))
		goto out;

	/* charge current (mA) */
	rc = max77818_charger_set_charge_current(charger,
		pdata->fast_charge_current);
	if (unlikely(IS_ERR_VALUE(rc)))
		goto out;

	/* input current limit (mA) */
	rc = max77818_charger_set_input_current(charger,
		pdata->input_current_limit);
	if (unlikely(IS_ERR_VALUE(rc)))
		goto out;

	/* topoff current(mA) and topoff timer(min) */
	rc = max77818_charger_set_topoff_current(charger,
		pdata->topoff_current, pdata->topoff_timer);
	if (unlikely(IS_ERR_VALUE(rc)))
		goto out;

	/* charge restart threshold(mV) and fast-charge timer(hr) */
	val = pdata->restart_threshold < 200 ?
		(int)(pdata->restart_threshold - 100)/50 : 0x03;

	temp_val = pdata->fast_charge_timer == 0 ? 0x00 :
		pdata->fast_charge_timer < 4 ? 0x01 :
		pdata->fast_charge_timer < 16 ?
		(int)DIV_ROUND_UP(pdata->fast_charge_timer - 4, 2) + 1 : 0x00;

	val = val<<M2SH(BIT_CHG_RSTRT) | temp_val<<M2SH(BIT_FCHGTIME);

	rc = regmap_update_bits(charger->regmap, REG_CHG_CNFG_01,
			(BIT_CHG_RSTRT | BIT_FCHGTIME), val);
	if (unlikely(IS_ERR_VALUE(rc)))
		goto out;

	/* charge termination voltage (mV) */
	val = pdata->termination_voltage < 3650 ? 0x00 :
		pdata->termination_voltage <= 4325 ?
		(int)DIV_ROUND_UP(
			pdata->termination_voltage - 3650, 25) :
		pdata->termination_voltage <= 4340 ? 0x1C :
		pdata->termination_voltage <= 4700 ?
		(int)DIV_ROUND_UP(
			pdata->termination_voltage - 3650, 25) + 1 : 0x2B;
	rc = regmap_update_bits(charger->regmap,
		REG_CHG_CNFG_04, BIT_CHG_CV_PRM,
		val << M2SH(BIT_CHG_CV_PRM));
	if (unlikely(IS_ERR_VALUE(rc)))
		goto out;

out:
	return;
}

struct max77818_charger_status_map {
	int health, status, charge_type;
};

static struct max77818_charger_status_map max77818_charger_status_map[] = {
#define STATUS_MAP(_chg_dtls, _health, _status, _charge_type) \
	[CHG_DTLS_##_chg_dtls] = {\
		.health = POWER_SUPPLY_HEALTH_##_health,\
		.status = POWER_SUPPLY_STATUS_##_status,\
		.charge_type = POWER_SUPPLY_CHARGE_TYPE_##_charge_type,\
	}
	/* chg_details_xx, health, status, charge_type */
	STATUS_MAP(PREQUAL, GOOD, CHARGING, TRICKLE),
	STATUS_MAP(FASTCHARGE_CC, GOOD, CHARGING, FAST),
	STATUS_MAP(FASTCHARGE_CV, GOOD, CHARGING, FAST),
	STATUS_MAP(TOPOFF, GOOD, CHARGING, FAST),
	STATUS_MAP(DONE, GOOD, FULL, NONE),
	STATUS_MAP(OFF_TIMER_FAULT, SAFETY_TIMER_EXPIRE,
		NOT_CHARGING, NONE),
	STATUS_MAP(OFF_SUSPEND, UNKNOWN, NOT_CHARGING, NONE),
	STATUS_MAP(OFF_INPUT_INVALID, UNKNOWN, NOT_CHARGING, NONE),
	STATUS_MAP(OFF_JUCTION_TEMP, UNKNOWN, NOT_CHARGING, UNKNOWN),
	STATUS_MAP(OFF_WDT_EXPIRED, WATCHDOG_TIMER_EXPIRE,
		NOT_CHARGING, UNKNOWN),
};

static int max77818_charger_update(struct max77818_charger_data *charger)
{
	int rc;
	u8 chg_details[3];
	u8 chg_dtls;

	charger->health      = POWER_SUPPLY_HEALTH_UNKNOWN;
	charger->status      = POWER_SUPPLY_STATUS_UNKNOWN;
	charger->charge_type = POWER_SUPPLY_CHARGE_TYPE_UNKNOWN;

	rc = max77818_bulk_read(charger->regmap,
		REG_CHG_DTLS_00, chg_details, 3);
	if (unlikely(IS_ERR_VALUE(rc))) {
		pr_err("CHG_DETAILS read error [%d]\n", rc);
		goto out;
	}

	pr_info("%s: chg_details 00=0x%x, 01=0x%x, 02=0x%x\n",
		__func__, chg_details[0], chg_details[1], chg_details[2]);

	charger->present = (max77818_charger_present_input(charger) ||
			max77818_wcharger_present_input(charger));
	if (unlikely(!charger->present)) {
		/* no charger present */
		charger->health      = POWER_SUPPLY_HEALTH_UNKNOWN;
		charger->status      = POWER_SUPPLY_STATUS_DISCHARGING;
		charger->charge_type = POWER_SUPPLY_CHARGE_TYPE_UNKNOWN;
		goto out;
	}

	chg_dtls = chg_details[1] & BIT_CHG_DTLS;

	charger->health =
		max77818_charger_status_map[chg_dtls].health;
	charger->status =
		max77818_charger_status_map[chg_dtls].status;
	charger->charge_type =
		max77818_charger_status_map[chg_dtls].charge_type;

	if (likely(charger->health != POWER_SUPPLY_HEALTH_UNKNOWN))
		goto out;

	/* override health by TREG */
	if ((chg_details[1] & BIT_TREG) != 0)
		charger->health = POWER_SUPPLY_HEALTH_OVERHEAT;

out:
	pr_info("%s: PRESENT %d HEALTH %d STATUS %d CHARGE_TYPE %d\n",
		__func__,
		charger->present, charger->health,
		charger->status, charger->charge_type);
	return rc;
}
static int max77818_charger_get_property(struct power_supply *psy,
		enum power_supply_property psp, union power_supply_propval *val)
{

	struct max77818_charger_data *charger =
		power_supply_get_drvdata(psy);
	int rc = 0;

	rc = max77818_charger_update(charger);
	if (unlikely(IS_ERR_VALUE(rc)))
		goto out;

	switch (psp) {
	case POWER_SUPPLY_PROP_ONLINE:
		val->intval = charger->present;
		break;

	case POWER_SUPPLY_PROP_HEALTH:
		val->intval = charger->health;
		break;

	case POWER_SUPPLY_PROP_STATUS:
		val->intval = charger->status;
		break;

	case POWER_SUPPLY_PROP_CHARGE_TYPE:
		val->intval = charger->charge_type;
		break;

	case POWER_SUPPLY_PROP_CURRENT_NOW:
		val->intval = max77818_charger_get_charge_current(charger);
		break;

	case POWER_SUPPLY_PROP_CURRENT_MAX:
		val->intval = max77818_charger_get_input_current(charger);
		break;

	default:
		rc = -EINVAL;
		goto out;
	}

out:
	pr_info("%s: <get_property> psp %d val %d [%d]\n",
		__func__, psp, val->intval, rc);
	return rc;
}

static int max77818_charger_set_property(struct power_supply *psy,
		enum power_supply_property psp,
		const union power_supply_propval *val)
{
	struct max77818_charger_data *charger =
		power_supply_get_drvdata(psy);
	int rc = 0;

	__lock(charger);

	switch (psp) {
	case POWER_SUPPLY_PROP_ONLINE:
		rc = max77818_charger_set_enable(charger, val->intval);
		if (unlikely(IS_ERR_VALUE(rc)))
			goto out;

		/* apply charge current */
		rc = max77818_charger_set_charge_current(charger,
			charger->pdata->fast_charge_current);
		break;

	case POWER_SUPPLY_PROP_CURRENT_NOW:
		/* val->intval - uA */
		rc = max77818_charger_set_charge_current(charger,
			val->intval/1000); /* mA */
		if (unlikely(IS_ERR_VALUE(rc)))
			goto out;
		charger->pdata->fast_charge_current =
			val->intval/1000;	/* mA */
		break;

	case POWER_SUPPLY_PROP_CURRENT_MAX:
		rc = max77818_charger_set_input_current(charger,
			val->intval/1000);	/* mA */
		if (unlikely(IS_ERR_VALUE(rc)))
			goto out;
		charger->pdata->input_current_limit =
			val->intval/1000;	/* mA */
		break;

	default:
		rc = -EINVAL;
		goto out;
	}

out:
	pr_info("%s: <set_property> psp %d val %d [%d]\n",
		__func__, psp, val->intval, rc);
	__unlock(charger);
	return rc;
}
/* interrupt handler and workqueu*/
static void max77818_do_irq(struct max77818_charger_data *charger, int irq)
{

	u8 val, chg_details[3];
	bool aicl_mode, chg_input, wc_input;

	chg_details[0] = charger->details_0;
	chg_details[1] = charger->details_1;
	chg_details[2] = charger->details_2;

	switch (irq) {
	case CHG_INT_AICL_I:
		max77818_read(charger->regmap, REG_CHG_INT_OK, &val);
		aicl_mode = (val & BIT_AICL_OK) ? false : true;
		pr_debug("CHG_INT_AICL: AICL %s\n",
			aicl_mode ? "Not aicl mode" : "aicl mode");
		break;

	case CHG_INT_CHGIN_I:
		chg_input = max77818_charger_present_input(charger);
		wc_input = max77818_wcharger_present_input(charger);

		if (chg_input) {
			/* charger insert */
			max77818_charger_init_dev(charger);
		} else {
			if (!wc_input) {
				/* charger remove */
				max77818_charger_exit_dev(charger);
			}
		}
		pr_debug("CHG_INT_CHGIN: Charger input %s\n",
			chg_input ? "inserted" : "removed");
		break;

	case CHG_INT_WCIN_I:
		chg_input = max77818_charger_present_input(charger);
		wc_input = max77818_wcharger_present_input(charger);

		if (wc_input) {
			if (!chg_input) {
				/* wireless charger only */
				max77818_charger_init_dev(charger);
			}
		} else {
			if (!chg_input) {
				/* wireless charger remove */
				max77818_charger_exit_dev(charger);
			}
		}
		pr_debug("CHG_INT_WCIN: Wireless charger input %s\n",
			wc_input ? "inserted" : "removed");
		break;

	case CHG_INT_CHG_I:
		/* do insert code here */
		val = (chg_details[1] & BIT_CHG_DTLS)>>FFS(BIT_CHG_DTLS);
		pr_debug("CHG_INT_CHG: chg_dtls = %02Xh\n", val);
		break;

	case CHG_INT_BAT_I:
		/* do insert code here */
		val = (chg_details[1] & BIT_BAT)>>FFS(BIT_BAT);
		pr_debug("CHG_INT_BAT: bat_dtls = %02Xh\n", val);
		break;

	case CHG_INT_BATP_I:
		/* do insert code here */
		val = (chg_details[0] & BIT_BATP)>>FFS(BIT_BATP);
		pr_debug("CHG_INT_BATP: battery %s\n",
				val ? "no presence" : "presence");
		break;

	case CHG_INT_BYP_I:
		/* do insert code here */
		val = (chg_details[2] & BIT_BYP)>>FFS(BIT_BYP);
		pr_debug("CHG_INT_BYP: byp_dtls = %02Xh\n", val);
		break;

	default:
		break;
	}
	/* notify psy changed */
	power_supply_changed(charger->psy_chg);
	return;

}

static void max77818_charger_irq_work(struct work_struct *work)
{
	struct max77818_charger_data *me =
		container_of(work, struct max77818_charger_data, irq_work.work);
	u8 irq;

	__lock(me);

	irq = me->irq - me->byp_irq;

	max77818_do_irq(me, irq);

	__unlock(me);
}

static irqreturn_t max77818_charger_chgin_isr(int irq, void *data)
{
	struct max77818_charger_data *me = data;
	u8 reg_details[3];

	me->irq = irq;

	max77818_bulk_read(me->regmap, REG_CHG_DTLS_00, reg_details, 3);
	pr_info("%s: chg_dtls[0]=0x%x, [1]=0x%x, [2]=0x%x\n",
		__func__, reg_details[0], reg_details[1], reg_details[2]);

	me->details_0 = reg_details[0];
	me->details_1 = reg_details[1];
	me->details_2 = reg_details[2];

	schedule_delayed_work(&me->irq_work, IRQ_WORK_DELAY);
	return IRQ_HANDLED;
}


static irqreturn_t max77818_charger_wcin_isr(int irq, void *data)
{
	struct max77818_charger_data *me = data;
	u8 reg_details[3];

	me->irq = irq;

	max77818_bulk_read(me->regmap, REG_CHG_DTLS_00, reg_details, 3);
	pr_info("%s: chg_dtls[0]=0x%x, [1]=0x%x, [2]=0x%x\n",
		__func__, reg_details[0], reg_details[1], reg_details[2]);

	me->details_0 = reg_details[0];
	me->details_1 = reg_details[1];
	me->details_2 = reg_details[2];

	schedule_delayed_work(&me->irq_work, IRQ_WORK_DELAY);
	return IRQ_HANDLED;
}


static irqreturn_t max77818_charger_aicl_isr(int irq, void *data)
{
	struct max77818_charger_data *me = data;
	u8 reg_details[3];

	me->irq = irq;

	max77818_bulk_read(me->regmap, REG_CHG_DTLS_00, reg_details, 3);
	pr_info("%s: chg_dtls[0]=0x%x, [1]=0x%x, [2]=0x%x\n",
		__func__, reg_details[0], reg_details[1], reg_details[2]);

	me->details_0 = reg_details[0];
	me->details_1 = reg_details[1];
	me->details_2 = reg_details[2];

	schedule_delayed_work(&me->irq_work, IRQ_WORK_DELAY);
	return IRQ_HANDLED;
}

#ifdef CONFIG_OF
static int max77818_charger_parse_dt(struct max77818_charger_data *charger)
{
	struct device *dev = charger->dev;
	struct device_node *np = of_find_node_by_name(NULL, "charger");
	struct max77818_charger_platform_data *pdata;

	int ret = 0;

	pdata = devm_kzalloc(dev, sizeof(*pdata), GFP_KERNEL);
	if (unlikely(pdata == NULL))
		return -ENOMEM;

	pdata->fast_charge_timer = 0;	/* disable */
	ret |= of_property_read_u32(np, "fast_charge_timer",
			&pdata->fast_charge_timer);
	pr_debug("property:FCHGTIME %uhour\n", pdata->fast_charge_timer);

	pdata->fast_charge_current = 500;	/* 500mA */
	ret |= of_property_read_u32(np, "fast_charge_current",
			&pdata->fast_charge_current);
	pr_debug("property:CHG_CC %umA\n", pdata->fast_charge_current);

	pdata->termination_voltage = 4350;	/* 4350mV */
	ret |= of_property_read_u32(np, "charge_termination_voltage",
			&pdata->termination_voltage);
	pr_debug("property:CHG_CV_PRM %umV\n",
			pdata->termination_voltage);

	pdata->topoff_timer = 0;	/* 0 min */
	ret |= of_property_read_u32(np, "topoff_timer",
			&pdata->topoff_timer);
	pr_debug("property:TOPOFF_TIME %umin\n", pdata->topoff_timer);

	pdata->topoff_current = 200;	/* 200mA */
	ret |= of_property_read_u32(np, "topoff_current",
			&pdata->topoff_current);
	pr_debug("property:TOPOFF_ITH %umA\n", pdata->topoff_current);

	pdata->restart_threshold = 150;	/* 150mV */
	ret |= of_property_read_u32(np, "restart_threshold",
			&pdata->restart_threshold);
	pr_debug("property:CHG_RSTRT %umV\n", pdata->restart_threshold);

	pdata->input_current_limit = 500; /* 500mA */
	ret |= of_property_read_u32(np, "input_current_limit",
			&pdata->input_current_limit);
	pr_debug("property:INPUT_CURRENT_LIMIT %umA\n",
		pdata->input_current_limit);

	if (ret < 0)
		return ret;
	charger->pdata = pdata;
	return 0;
}
#endif

static int max77818_charger_probe(struct platform_device *pdev)
{
	struct max77818_dev *max77818 = dev_get_drvdata(pdev->dev.parent);
	struct max77818_charger_platform_data *pdata;
	struct max77818_charger_data *charger;
	int ret = 0;
	u8 val = 0;
	struct power_supply_config charger_cfg;

	pr_info("%s: Max77818 Charger Driver Loading\n", __func__);

	charger = kzalloc(sizeof(*charger), GFP_KERNEL);
	if (!charger)
		return -ENOMEM;

	pdata = devm_kzalloc(&pdev->dev, sizeof(*pdata), GFP_KERNEL);
	if (unlikely(!pdata)) {
		pr_err("%s: out of memory\n", __func__);
		pdata = ERR_PTR(-ENOMEM);
		return -ENOMEM;
	}

	mutex_init(&charger->lock);

	charger->dev = &pdev->dev;
	charger->regmap = max77818->regmap_chg;
	charger->pdata = pdata;

#if defined(CONFIG_OF)
	ret = max77818_charger_parse_dt(charger);
	if (ret < 0) {
		pr_err("%s not found charger dt! ret[%d]\n",
				__func__, ret);
	}
#else
	pdata = dev_get_platdata(&pdev->dev);
#endif

	platform_set_drvdata(pdev, charger);
	charger->psy_chg_d.name		= "max77818-charger";
	charger->psy_chg_d.type		= POWER_SUPPLY_TYPE_UNKNOWN;
	charger->psy_chg_d.get_property	= max77818_charger_get_property;
	charger->psy_chg_d.set_property	= max77818_charger_set_property;
	charger->psy_chg_d.properties	= max77818_charger_props;
	charger->psy_chg_d.num_properties	=
		ARRAY_SIZE(max77818_charger_props);
	charger_cfg.drv_data = charger;
	charger_cfg.supplied_to = chg_supplied_to;
	charger_cfg.of_node = max77818->dev->of_node;
	charger_cfg.num_supplicants = ARRAY_SIZE(chg_supplied_to);

	max77818_charger_initialize(charger);

	charger->psy_chg =
		devm_power_supply_register(max77818->dev,
				&charger->psy_chg_d,
				&charger_cfg);
	if (IS_ERR(charger->psy_chg)) {
		pr_err("Couldn't register psy_chg rc=%ld\n",
				PTR_ERR(charger->psy_chg));
		goto err_power_supply_register;
	}

	INIT_DELAYED_WORK(&charger->irq_work, max77818_charger_irq_work);

	charger->byp_irq = regmap_irq_get_virq(max77818->irqc_chg,
		CHG_IRQ_BYP_I);
	charger->chgin_irq = regmap_irq_get_virq(max77818->irqc_chg,
		CHG_IRQ_CHGIN_I);
	charger->wcin_irq = regmap_irq_get_virq(max77818->irqc_chg,
		CHG_IRQ_WCIN_I);
	charger->aicl_irq = regmap_irq_get_virq(max77818->irqc_chg,
		CHG_IRQ_AICL_I);

	ret = request_threaded_irq(charger->chgin_irq, NULL,
			max77818_charger_chgin_isr,
			IRQF_TRIGGER_FALLING, "charger-chgin", charger);
	if (ret) {
		pr_err("%s: Failed to Reqeust CHGIN IRQ\n", __func__);
		goto err_chgin_irq;
	}

	ret = request_threaded_irq(charger->wcin_irq, NULL,
			max77818_charger_wcin_isr,
			IRQF_TRIGGER_FALLING, "charger-wcin", charger);
	if (ret) {
		pr_err("%s: Failed to Reqeust WCIN IRQ\n", __func__);
		goto err_wcin_irq;
	}

	ret = request_threaded_irq(charger->aicl_irq, NULL,
		max77818_charger_aicl_isr,
		IRQF_TRIGGER_FALLING, "charger-aicl", charger);
	if (ret) {
		pr_err("%s: Failed to Reqeust AICL IRQ\n", __func__);
		goto err_aicl_irq;
	}

	max77818_read(charger->regmap, REG_CHG_INT, &val);
	pr_info("%s: Max77818 Charger CHG_INT=%Xh\n", __func__, val);

	max77818_read(charger->regmap, REG_CHG_INT_MASK, &val);
	pr_info("%s: Max77818 Charger CHG_INT_MASK=%Xh\n", __func__, val);

	pr_info("%s: Max77818 Charger Driver Loaded\n", __func__);

	return 0;

err_aicl_irq:
	free_irq(charger->wcin_irq, NULL);
err_wcin_irq:
	free_irq(charger->chgin_irq, NULL);
err_chgin_irq:
	power_supply_unregister(charger->psy_chg);
err_power_supply_register:
	kfree(charger);

	return ret;
}

static int max77818_charger_remove(struct platform_device *pdev)
{
	struct max77818_charger_data *charger =
		platform_get_drvdata(pdev);

	free_irq(charger->chgin_irq, NULL);
	free_irq(charger->wcin_irq, NULL);
	free_irq(charger->aicl_irq, NULL);
	power_supply_unregister(charger->psy_chg);

	kfree(charger);

	return 0;
}

#if defined CONFIG_PM
static int max77818_charger_suspend(struct device *dev)
{
	return 0;
}

static int max77818_charger_resume(struct device *dev)
{
	return 0;
}
#else
#define max77818_charger_suspend NULL
#define max77818_charger_resume NULL
#endif

#ifdef CONFIG_OF
static const struct of_device_id max77818_charger_dt_ids[] = {
	{ .compatible = "maxim,max77818-charger" },
	{ }
};
MODULE_DEVICE_TABLE(of, max77818_charger_dt_ids);
#endif

static SIMPLE_DEV_PM_OPS(max77818_charger_pm_ops, max77818_charger_suspend,
		max77818_charger_resume);

static struct platform_driver max77818_charger_driver = {
	.driver = {
		.name = MAX77818_CHARGER_NAME,
		.owner = THIS_MODULE,
#ifdef CONFIG_PM
		.pm = &max77818_charger_pm_ops,
#endif
#ifdef CONFIG_OF
		.of_match_table = max77818_charger_dt_ids,
#endif
	},
	.probe = max77818_charger_probe,
	.remove = max77818_charger_remove,
};

static int __init max77818_charger_init(void)
{
	pr_info("%s\n", __func__);
	return platform_driver_register(&max77818_charger_driver);
}

static void __exit max77818_charger_exit(void)
{
	platform_driver_unregister(&max77818_charger_driver);
}

module_init(max77818_charger_init);
module_exit(max77818_charger_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_VERSION(DRIVER_VERSION);
