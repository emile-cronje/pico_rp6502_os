/*
 * Copyright (c) 2024, Custom bare-metal OSAL configuration for Raspberry Pi Pico SDK
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef USB_OSAL_PICO_BAREMETAL_CONFIG_H
#define USB_OSAL_PICO_BAREMETAL_CONFIG_H

/* ========================================================================= */
/* CONFIGURATION OPTIONS */
/* ========================================================================= */

/**
 * @brief Maximum number of threads that can be created
 * In bare-metal environment, threads are cooperative and stored in a list
 */
#ifndef CONFIG_USB_OSAL_MAX_THREADS
#define CONFIG_USB_OSAL_MAX_THREADS 8
#endif

/**
 * @brief Maximum number of timers that can be created
 * Timers use Pico SDK hardware timer callbacks
 */
#ifndef CONFIG_USB_OSAL_MAX_TIMERS
#define CONFIG_USB_OSAL_MAX_TIMERS 16
#endif

/**
 * @brief Default semaphore maximum count
 * Set to 1 for binary semaphores, higher for counting semaphores
 */
#ifndef CONFIG_USB_OSAL_SEM_MAX_COUNT
#define CONFIG_USB_OSAL_SEM_MAX_COUNT 1
#endif

/**
 * @brief Polling interval in microseconds for blocking operations
 * Lower values provide better responsiveness but higher CPU usage
 */
#ifndef CONFIG_USB_OSAL_POLL_INTERVAL_US
#define CONFIG_USB_OSAL_POLL_INTERVAL_US 100
#endif

/**
 * @brief Enable debug logging for OSAL operations
 * Set to 1 to enable debug messages, 0 to disable
 */
#ifndef CONFIG_USB_OSAL_DEBUG
#define CONFIG_USB_OSAL_DEBUG 0
#endif

/**
 * @brief Memory alignment for USB buffers
 * Should match the alignment requirements of the USB controller
 */
#ifndef CONFIG_USB_OSAL_ALIGN_SIZE
#define CONFIG_USB_OSAL_ALIGN_SIZE 4
#endif

/**
 * @brief Enable cooperative threading support
 * When enabled, usb_osal_thread_schedule_other() will execute pending tasks
 */
#ifndef CONFIG_USB_OSAL_COOPERATIVE_THREADING
#define CONFIG_USB_OSAL_COOPERATIVE_THREADING 1
#endif

/**
 * @brief Thread execution mode
 * 0 = Simple function call mode (thread function called once)
 * 1 = Continuous execution mode (thread function called repeatedly)
 */
#ifndef CONFIG_USB_OSAL_THREAD_CONTINUOUS
#define CONFIG_USB_OSAL_THREAD_CONTINUOUS 0
#endif

/* ========================================================================= */
/* VALIDATION */
/* ========================================================================= */

#if CONFIG_USB_OSAL_MAX_THREADS < 1
#error "CONFIG_USB_OSAL_MAX_THREADS must be at least 1"
#endif

#if CONFIG_USB_OSAL_MAX_TIMERS < 1
#error "CONFIG_USB_OSAL_MAX_TIMERS must be at least 1"
#endif

#if CONFIG_USB_OSAL_POLL_INTERVAL_US < 10
#error "CONFIG_USB_OSAL_POLL_INTERVAL_US must be at least 10 microseconds"
#endif

/* ========================================================================= */
/* MEMORY ALLOCATION CONFIGURATION */
/* ========================================================================= */

/**
 * @brief Use static memory allocation instead of malloc/free
 * When enabled, all OSAL objects use pre-allocated static memory pools
 * This is safer for bare-metal environments where dynamic allocation may fail
 */
#ifndef CONFIG_USB_OSAL_STATIC_MEMORY
#define CONFIG_USB_OSAL_STATIC_MEMORY 0
#endif

#if CONFIG_USB_OSAL_STATIC_MEMORY

/**
 * @brief Static memory pool sizes
 */
#ifndef CONFIG_USB_OSAL_STATIC_SEMAPHORE_COUNT
#define CONFIG_USB_OSAL_STATIC_SEMAPHORE_COUNT 16
#endif

#ifndef CONFIG_USB_OSAL_STATIC_MUTEX_COUNT
#define CONFIG_USB_OSAL_STATIC_MUTEX_COUNT 8
#endif

#ifndef CONFIG_USB_OSAL_STATIC_MQ_COUNT
#define CONFIG_USB_OSAL_STATIC_MQ_COUNT 8
#endif

#ifndef CONFIG_USB_OSAL_STATIC_MQ_MAX_SIZE
#define CONFIG_USB_OSAL_STATIC_MQ_MAX_SIZE 32
#endif

#endif /* CONFIG_USB_OSAL_STATIC_MEMORY */

/* ========================================================================= */
/* PLATFORM SPECIFIC CONFIGURATION */
/* ========================================================================= */

/**
 * @brief Core selection for multicore operations
 * 0 = Use core 0 only
 * 1 = Use core 1 only
 * -1 = Use current core
 */
#ifndef CONFIG_USB_OSAL_CORE_AFFINITY
#define CONFIG_USB_OSAL_CORE_AFFINITY -1
#endif

/**
 * @brief Enable multicore synchronization
 * When enabled, critical sections will work correctly across both cores
 */
#ifndef CONFIG_USB_OSAL_MULTICORE
#define CONFIG_USB_OSAL_MULTICORE 1
#endif

/**
 * @brief Timer precision mode
 * 0 = Use standard precision (millisecond resolution)
 * 1 = Use high precision (microsecond resolution where possible)
 */
#ifndef CONFIG_USB_OSAL_HIGH_PRECISION_TIMERS
#define CONFIG_USB_OSAL_HIGH_PRECISION_TIMERS 0
#endif

#endif /* USB_OSAL_PICO_BAREMETAL_CONFIG_H */
