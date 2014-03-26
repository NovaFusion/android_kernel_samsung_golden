/*
 * Copyright (C) ST Ericsson SA 2011
 *
 * License Terms: GNU General Public License v2
 *
 */
#ifndef __LINUX_REGULATOR_DBX500_H
#define __LINUX_REGULATOR_DBX500_H

struct ux500_regulator;

#ifdef CONFIG_REGULATOR
/*
 * NOTE! The device will be connected to the correct regulator by this
 * new framework. A list with connections will match up dev_name(dev)
 * to the specific regulator. This follows the same principle as the
 * normal regulator framework.
 *
 * This framework shall only be used in special cases when a regulator
 * has to be enabled/disabled in atomic context.
 */

/**
 * ux500_regulator_get()
 *
 * @dev: Drivers device struct
 *
 * Returns a ux500_regulator struct. Shall be used as argument for
 * ux500_regulator_atomic_enable/disable calls.
 * Return ERR_PTR(-EINVAL) upon no matching regulator found.
 */
struct ux500_regulator *__must_check ux500_regulator_get(struct device *dev);

/**
 * ux500_regulator_atomic_enable()
 *
 * @regulator: Regulator handle, provided from ux500_regulator_get.
 *
 * The enable/disable functions keep an internal counter, so every
 * enable must be paired with an disable in order to turn off regulator.
 */
int ux500_regulator_atomic_enable(struct ux500_regulator *regulator);

/**
 * ux500_regulator_atomic_disable()
 *
 * @regulator: Regulator handle, provided from ux500_regulator_get.
 *
 */
int ux500_regulator_atomic_disable(struct ux500_regulator *regulator);

/**
 * ux500_regulator_put()
 *
 * @regulator: Regulator handle, provided from ux500_regulator_get.
 */
void ux500_regulator_put(struct ux500_regulator *regulator);

#else

static inline struct ux500_regulator *__must_check
ux500_regulator_get(struct device *dev)
{
	return ERR_PTR(-EINVAL);
}

static inline int
ux500_regulator_atomic_enable(struct ux500_regulator *regulator)
{
	return -EINVAL;
}

static inline int
ux500_regulator_atomic_disable(struct ux500_regulator *regulator)
{
	return -EINVAL;
}

static inline void ux500_regulator_put(struct ux500_regulator *regulator)
{
}
#endif /* CONFIG_REGULATOR */

#ifdef CONFIG_REGULATOR_DEBUG
void ux500_regulator_suspend_debug(void);
void ux500_regulator_resume_debug(void);
#else
static inline void ux500_regulator_suspend_debug(void) { }
static inline void ux500_regulator_resume_debug(void) { }
#endif

#endif
