/*
 * Copyright (c) 2024, Custom bare-metal OSAL for Raspberry Pi Pico SDK
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "usb_osal.h"
#include "usb_errno.h"
#include "usb_config.h"
#include "usb_log.h"
#include "usb_osal_pico.h"

#include "pico/stdlib.h"
#include "pico/time.h"
#include "pico/platform.h"
#include "pico/sync.h"
#include "pico/critical_section.h"
#include "hardware/timer.h"
#include "hardware/irq.h"

#include <stdlib.h>
#include <string.h>

/* Debug logging macros */
#if CONFIG_USB_OSAL_DEBUG
#define OSAL_LOG_DBG(...) USB_LOG_DBG(__VA_ARGS__)
#define OSAL_LOG_INFO(...) USB_LOG_INFO(__VA_ARGS__)
#define OSAL_LOG_WRN(...) USB_LOG_WRN(__VA_ARGS__)
#define OSAL_LOG_ERR(...) USB_LOG_ERR(__VA_ARGS__)
#else
#define OSAL_LOG_DBG(...)
#define OSAL_LOG_INFO(...)
#define OSAL_LOG_WRN(...)
#define OSAL_LOG_ERR(...) USB_LOG_ERR(__VA_ARGS__)
#endif

/* ========================================================================= */
/* STATIC MEMORY MANAGEMENT (OPTIONAL) */
/* ========================================================================= */

#if CONFIG_USB_OSAL_STATIC_MEMORY

typedef struct
{
    pico_semaphore_t sem;
    bool in_use;
} static_semaphore_t;

typedef struct
{
    pico_mutex_t mutex;
    bool in_use;
} static_mutex_t;

typedef struct
{
    pico_mq_t mq;
    uintptr_t buffer[CONFIG_USB_OSAL_STATIC_MQ_MAX_SIZE];
    bool in_use;
} static_mq_t;

static static_semaphore_t static_semaphores[CONFIG_USB_OSAL_STATIC_SEMAPHORE_COUNT];
static static_mutex_t static_mutexes[CONFIG_USB_OSAL_STATIC_MUTEX_COUNT];
static static_mq_t static_mqs[CONFIG_USB_OSAL_STATIC_MQ_COUNT];
static critical_section_t static_memory_lock;

static void *static_alloc_semaphore(void)
{
    critical_section_enter_blocking(&static_memory_lock);
    for (int i = 0; i < CONFIG_USB_OSAL_STATIC_SEMAPHORE_COUNT; i++)
    {
        if (!static_semaphores[i].in_use)
        {
            static_semaphores[i].in_use = true;
            critical_section_exit(&static_memory_lock);
            return &static_semaphores[i].sem;
        }
    }
    critical_section_exit(&static_memory_lock);
    return NULL;
}

static void static_free_semaphore(void *ptr)
{
    critical_section_enter_blocking(&static_memory_lock);
    for (int i = 0; i < CONFIG_USB_OSAL_STATIC_SEMAPHORE_COUNT; i++)
    {
        if (&static_semaphores[i].sem == ptr)
        {
            static_semaphores[i].in_use = false;
            break;
        }
    }
    critical_section_exit(&static_memory_lock);
}

static void *static_alloc_mutex(void)
{
    critical_section_enter_blocking(&static_memory_lock);
    for (int i = 0; i < CONFIG_USB_OSAL_STATIC_MUTEX_COUNT; i++)
    {
        if (!static_mutexes[i].in_use)
        {
            static_mutexes[i].in_use = true;
            critical_section_exit(&static_memory_lock);
            return &static_mutexes[i].mutex;
        }
    }
    critical_section_exit(&static_memory_lock);
    return NULL;
}

static void static_free_mutex(void *ptr)
{
    critical_section_enter_blocking(&static_memory_lock);
    for (int i = 0; i < CONFIG_USB_OSAL_STATIC_MUTEX_COUNT; i++)
    {
        if (&static_mutexes[i].mutex == ptr)
        {
            static_mutexes[i].in_use = false;
            break;
        }
    }
    critical_section_exit(&static_memory_lock);
}

static void *static_alloc_mq(uint32_t max_msgs)
{
    if (max_msgs > CONFIG_USB_OSAL_STATIC_MQ_MAX_SIZE)
        return NULL;

    critical_section_enter_blocking(&static_memory_lock);
    for (int i = 0; i < CONFIG_USB_OSAL_STATIC_MQ_COUNT; i++)
    {
        if (!static_mqs[i].in_use)
        {
            static_mqs[i].in_use = true;
            static_mqs[i].mq.buffer = static_mqs[i].buffer;
            critical_section_exit(&static_memory_lock);
            return &static_mqs[i].mq;
        }
    }
    critical_section_exit(&static_memory_lock);
    return NULL;
}

static void static_free_mq(void *ptr)
{
    critical_section_enter_blocking(&static_memory_lock);
    for (int i = 0; i < CONFIG_USB_OSAL_STATIC_MQ_COUNT; i++)
    {
        if (&static_mqs[i].mq == ptr)
        {
            static_mqs[i].in_use = false;
            break;
        }
    }
    critical_section_exit(&static_memory_lock);
}

#endif /* CONFIG_USB_OSAL_STATIC_MEMORY */

/* ========================================================================= */
/* BARE-METAL THREAD SIMULATION */
/* ========================================================================= */

// Simple linked list for thread/task management in bare-metal environment
typedef struct pico_thread
{
    const char *name;
    usb_thread_entry_t entry;
    void *args;
    uint32_t stack_size;
    uint32_t prio;
    bool active;
    uint32_t exec_count;
    struct pico_thread *next;
} pico_thread_t;

static pico_thread_t *thread_list = NULL;
static critical_section_t thread_lock;
static bool osal_initialized = false;

/* ========================================================================= */
/* SEMAPHORE IMPLEMENTATION */
/* ========================================================================= */

typedef struct
{
    volatile uint32_t count;
    uint32_t max_count;
    critical_section_t lock;
    bool initialized;
} pico_semaphore_t;

/* ========================================================================= */
/* MUTEX IMPLEMENTATION */
/* ========================================================================= */

typedef struct
{
    volatile bool locked;
    critical_section_t lock;
    bool initialized;
} pico_mutex_t;

/* ========================================================================= */
/* MESSAGE QUEUE IMPLEMENTATION */
/* ========================================================================= */

typedef struct
{
    uintptr_t *buffer;
    uint32_t size;
    volatile uint32_t head;
    volatile uint32_t tail;
    volatile uint32_t count;
    critical_section_t lock;
    bool initialized;
    bool static_buffer;
} pico_mq_t;

/* ========================================================================= */
/* TIMER IMPLEMENTATION */
/* ========================================================================= */

typedef struct pico_timer_node
{
    struct usb_osal_timer timer;
    struct repeating_timer rpt_timer;
    bool active;
    struct pico_timer_node *next;
} pico_timer_node_t;

static pico_timer_node_t *timer_list = NULL;
static critical_section_t timer_lock;

/* ========================================================================= */
/* INTERNAL HELPER FUNCTIONS */
/* ========================================================================= */

static void ensure_osal_initialized(void)
{
    if (!osal_initialized)
    {
        critical_section_init(&thread_lock);
        critical_section_init(&timer_lock);

#if CONFIG_USB_OSAL_STATIC_MEMORY
        critical_section_init(&static_memory_lock);
        // Initialize static memory pools
        memset(static_semaphores, 0, sizeof(static_semaphores));
        memset(static_mutexes, 0, sizeof(static_mutexes));
        memset(static_mqs, 0, sizeof(static_mqs));
#endif

        osal_initialized = true;
        OSAL_LOG_INFO("Pico bare-metal OSAL initialized\r\n");
    }
}

static bool timer_callback(struct repeating_timer *rt)
{
    pico_timer_node_t *node = (pico_timer_node_t *)rt->user_data;
    if (node && node->active && node->timer.handler)
    {
        node->timer.handler(node->timer.argument);
    }
    return node->timer.is_period; // Return true for periodic timers
}

/* ========================================================================= */
/* THREAD FUNCTIONS */
/* ========================================================================= */

usb_osal_thread_t usb_osal_thread_create(const char *name, uint32_t stack_size, uint32_t prio, usb_thread_entry_t entry, void *args)
{
    ensure_osal_initialized();

    pico_thread_t *thread = (pico_thread_t *)malloc(sizeof(pico_thread_t));
    if (thread == NULL)
    {
        OSAL_LOG_ERR("Create thread %s failed - no memory\r\n", name ? name : "unnamed");
        return NULL;
    }

    thread->name = name ? name : "unnamed";
    thread->entry = entry;
    thread->args = args;
    thread->stack_size = stack_size;
    thread->prio = prio;
    thread->active = true;
    thread->exec_count = 0;

    // Add to thread list
    critical_section_enter_blocking(&thread_lock);
    thread->next = thread_list;
    thread_list = thread;
    critical_section_exit(&thread_lock);

    // In bare-metal, we just store the thread info but don't actually create an OS thread
    // The thread will be executed when usb_osal_thread_schedule_other() is called
    OSAL_LOG_DBG("Created thread %s (prio=%u, stack=%u)\r\n", thread->name, prio, stack_size);
    return (usb_osal_thread_t)thread;
}

void usb_osal_thread_delete(usb_osal_thread_t thread)
{
    if (thread == NULL)
        return;

    pico_thread_t *pico_thread = (pico_thread_t *)thread;

    critical_section_enter_blocking(&thread_lock);

    // Remove from thread list
    if (thread_list == pico_thread)
    {
        thread_list = pico_thread->next;
    }
    else
    {
        pico_thread_t *current = thread_list;
        while (current && current->next != pico_thread)
        {
            current = current->next;
        }
        if (current)
        {
            current->next = pico_thread->next;
        }
    }

    critical_section_exit(&thread_lock);

    free(pico_thread);
}

void usb_osal_thread_schedule_other(void)
{
    // In bare-metal environment, this would typically yield to other tasks
    // For Pico bare-metal, we can use a simple cooperative scheduler approach
    // or just process pending USB tasks

    critical_section_enter_blocking(&thread_lock);
    pico_thread_t *current = thread_list;
    critical_section_exit(&thread_lock);

#if CONFIG_USB_OSAL_COOPERATIVE_THREADING
    // Execute one iteration of each active thread
    while (current)
    {
        if (current->active && current->entry)
        {
            OSAL_LOG_DBG("Executing thread %s (count=%u)\r\n", current->name, current->exec_count);

#if CONFIG_USB_OSAL_THREAD_CONTINUOUS
            // Continuous execution mode - call thread function repeatedly
            current->entry(current->args);
            current->exec_count++;
#else
            // One-shot execution mode - call thread function once
            if (current->exec_count == 0)
            {
                current->entry(current->args);
                current->exec_count++;
                current->active = false; // Mark as completed
            }
#endif
        }
        current = current->next;
    }
#endif /* CONFIG_USB_OSAL_COOPERATIVE_THREADING */
}

/* ========================================================================= */
/* SEMAPHORE FUNCTIONS */
/* ========================================================================= */

usb_osal_sem_t usb_osal_sem_create(uint32_t initial_count)
{
#if CONFIG_USB_OSAL_STATIC_MEMORY
    pico_semaphore_t *sem = (pico_semaphore_t *)static_alloc_semaphore();
#else
    pico_semaphore_t *sem = (pico_semaphore_t *)malloc(sizeof(pico_semaphore_t));
#endif

    if (sem == NULL)
    {
        OSAL_LOG_ERR("Create semaphore failed - no memory\r\n");
        return NULL;
    }

    sem->count = initial_count;
    sem->max_count = CONFIG_USB_OSAL_SEM_MAX_COUNT;
    sem->initialized = true;
    critical_section_init(&sem->lock);

    OSAL_LOG_DBG("Created semaphore (initial=%u, max=%u)\r\n", initial_count, sem->max_count);
    return (usb_osal_sem_t)sem;
}

void usb_osal_sem_delete(usb_osal_sem_t sem)
{
    if (sem)
    {
        pico_semaphore_t *pico_sem = (pico_semaphore_t *)sem;
        if (pico_sem->initialized)
        {
            critical_section_deinit(&pico_sem->lock);
            pico_sem->initialized = false;
        }

#if CONFIG_USB_OSAL_STATIC_MEMORY
        static_free_semaphore(pico_sem);
#else
        free(pico_sem);
#endif
        OSAL_LOG_DBG("Deleted semaphore\r\n");
    }
}

int usb_osal_sem_take(usb_osal_sem_t sem, uint32_t timeout)
{
    if (sem == NULL)
        return -USB_ERR_INVAL;

    pico_semaphore_t *pico_sem = (pico_semaphore_t *)sem;
    if (!pico_sem->initialized)
        return -USB_ERR_INVAL;

    absolute_time_t timeout_time = make_timeout_time_ms(timeout == USB_OSAL_WAITING_FOREVER ? UINT32_MAX : timeout);

    while (true)
    {
        critical_section_enter_blocking(&pico_sem->lock);
        if (pico_sem->count > 0)
        {
            pico_sem->count--;
            critical_section_exit(&pico_sem->lock);
            OSAL_LOG_DBG("Semaphore taken (count now %u)\r\n", pico_sem->count);
            return 0;
        }
        critical_section_exit(&pico_sem->lock);

        if (timeout != USB_OSAL_WAITING_FOREVER && absolute_time_diff_us(get_absolute_time(), timeout_time) <= 0)
        {
            OSAL_LOG_DBG("Semaphore take timeout\r\n");
            return -USB_ERR_TIMEOUT;
        }

        // Small delay to prevent busy waiting
        sleep_us(CONFIG_USB_OSAL_POLL_INTERVAL_US);
    }
}

int usb_osal_sem_give(usb_osal_sem_t sem)
{
    if (sem == NULL)
        return -USB_ERR_INVAL;

    pico_semaphore_t *pico_sem = (pico_semaphore_t *)sem;
    if (!pico_sem->initialized)
        return -USB_ERR_INVAL;

    critical_section_enter_blocking(&pico_sem->lock);
    if (pico_sem->count < pico_sem->max_count)
    {
        pico_sem->count++;
        OSAL_LOG_DBG("Semaphore given (count now %u)\r\n", pico_sem->count);
    }
    critical_section_exit(&pico_sem->lock);

    return 0;
}

void usb_osal_sem_reset(usb_osal_sem_t sem)
{
    if (sem == NULL)
        return;

    pico_semaphore_t *pico_sem = (pico_semaphore_t *)sem;
    if (!pico_sem->initialized)
        return;

    critical_section_enter_blocking(&pico_sem->lock);
    pico_sem->count = 0;
    critical_section_exit(&pico_sem->lock);

    OSAL_LOG_DBG("Semaphore reset\r\n");
}

/* ========================================================================= */
/* MUTEX FUNCTIONS */
/* ========================================================================= */

usb_osal_mutex_t usb_osal_mutex_create(void)
{
#if CONFIG_USB_OSAL_STATIC_MEMORY
    pico_mutex_t *mutex = (pico_mutex_t *)static_alloc_mutex();
#else
    pico_mutex_t *mutex = (pico_mutex_t *)malloc(sizeof(pico_mutex_t));
#endif

    if (mutex == NULL)
    {
        OSAL_LOG_ERR("Create mutex failed - no memory\r\n");
        return NULL;
    }

    mutex->locked = false;
    mutex->initialized = true;
    critical_section_init(&mutex->lock);

    OSAL_LOG_DBG("Created mutex\r\n");
    return (usb_osal_mutex_t)mutex;
}

void usb_osal_mutex_delete(usb_osal_mutex_t mutex)
{
    if (mutex)
    {
        pico_mutex_t *pico_mutex = (pico_mutex_t *)mutex;
        if (pico_mutex->initialized)
        {
            critical_section_deinit(&pico_mutex->lock);
            pico_mutex->initialized = false;
        }

#if CONFIG_USB_OSAL_STATIC_MEMORY
        static_free_mutex(pico_mutex);
#else
        free(pico_mutex);
#endif
        OSAL_LOG_DBG("Deleted mutex\r\n");
    }
}

int usb_osal_mutex_take(usb_osal_mutex_t mutex)
{
    if (mutex == NULL)
        return -USB_ERR_INVAL;

    pico_mutex_t *pico_mutex = (pico_mutex_t *)mutex;
    if (!pico_mutex->initialized)
        return -USB_ERR_INVAL;

    while (true)
    {
        critical_section_enter_blocking(&pico_mutex->lock);
        if (!pico_mutex->locked)
        {
            pico_mutex->locked = true;
            critical_section_exit(&pico_mutex->lock);
            OSAL_LOG_DBG("Mutex taken\r\n");
            return 0;
        }
        critical_section_exit(&pico_mutex->lock);

        // Small delay to prevent busy waiting
        sleep_us(CONFIG_USB_OSAL_POLL_INTERVAL_US);
    }
}

int usb_osal_mutex_give(usb_osal_mutex_t mutex)
{
    if (mutex == NULL)
        return -USB_ERR_INVAL;

    pico_mutex_t *pico_mutex = (pico_mutex_t *)mutex;
    if (!pico_mutex->initialized)
        return -USB_ERR_INVAL;

    critical_section_enter_blocking(&pico_mutex->lock);
    pico_mutex->locked = false;
    critical_section_exit(&pico_mutex->lock);

    OSAL_LOG_DBG("Mutex released\r\n");
    return 0;
}

/* ========================================================================= */
/* MESSAGE QUEUE FUNCTIONS */
/* ========================================================================= */

usb_osal_mq_t usb_osal_mq_create(uint32_t max_msgs)
{
#if CONFIG_USB_OSAL_STATIC_MEMORY
    pico_mq_t *mq = (pico_mq_t *)static_alloc_mq(max_msgs);
    if (mq == NULL)
    {
        OSAL_LOG_ERR("Create message queue failed - no static memory (max_msgs=%u)\r\n", max_msgs);
        return NULL;
    }
    mq->static_buffer = true;
#else
    pico_mq_t *mq = (pico_mq_t *)malloc(sizeof(pico_mq_t));
    if (mq == NULL)
    {
        OSAL_LOG_ERR("Create message queue failed - no memory\r\n");
        return NULL;
    }

    mq->buffer = (uintptr_t *)malloc(max_msgs * sizeof(uintptr_t));
    if (mq->buffer == NULL)
    {
        free(mq);
        OSAL_LOG_ERR("Create message queue buffer failed - no memory (max_msgs=%u)\r\n", max_msgs);
        return NULL;
    }
    mq->static_buffer = false;
#endif

    mq->size = max_msgs;
    mq->head = 0;
    mq->tail = 0;
    mq->count = 0;
    mq->initialized = true;
    critical_section_init(&mq->lock);

    OSAL_LOG_DBG("Created message queue (size=%u)\r\n", max_msgs);
    return (usb_osal_mq_t)mq;
}

void usb_osal_mq_delete(usb_osal_mq_t mq)
{
    if (mq)
    {
        pico_mq_t *pico_mq = (pico_mq_t *)mq;
        if (pico_mq->initialized)
        {
            critical_section_deinit(&pico_mq->lock);
            pico_mq->initialized = false;
        }

#if CONFIG_USB_OSAL_STATIC_MEMORY
        static_free_mq(pico_mq);
#else
        if (!pico_mq->static_buffer)
        {
            free(pico_mq->buffer);
        }
        free(pico_mq);
#endif
        OSAL_LOG_DBG("Deleted message queue\r\n");
    }
}

int usb_osal_mq_send(usb_osal_mq_t mq, uintptr_t addr)
{
    if (mq == NULL)
        return -USB_ERR_INVAL;

    pico_mq_t *pico_mq = (pico_mq_t *)mq;
    if (!pico_mq->initialized)
        return -USB_ERR_INVAL;

    critical_section_enter_blocking(&pico_mq->lock);

    if (pico_mq->count >= pico_mq->size)
    {
        critical_section_exit(&pico_mq->lock);
        OSAL_LOG_DBG("Message queue send failed - queue full\r\n");
        return -USB_ERR_BUSY; // Queue full
    }

    pico_mq->buffer[pico_mq->tail] = addr;
    pico_mq->tail = (pico_mq->tail + 1) % pico_mq->size;
    pico_mq->count++;

    critical_section_exit(&pico_mq->lock);
    OSAL_LOG_DBG("Message sent to queue (count now %u)\r\n", pico_mq->count);
    return 0;
}

int usb_osal_mq_recv(usb_osal_mq_t mq, uintptr_t *addr, uint32_t timeout)
{
    if (mq == NULL || addr == NULL)
        return -USB_ERR_INVAL;

    pico_mq_t *pico_mq = (pico_mq_t *)mq;
    if (!pico_mq->initialized)
        return -USB_ERR_INVAL;

    absolute_time_t timeout_time = make_timeout_time_ms(timeout == USB_OSAL_WAITING_FOREVER ? UINT32_MAX : timeout);

    while (true)
    {
        critical_section_enter_blocking(&pico_mq->lock);

        if (pico_mq->count > 0)
        {
            *addr = pico_mq->buffer[pico_mq->head];
            pico_mq->head = (pico_mq->head + 1) % pico_mq->size;
            pico_mq->count--;
            critical_section_exit(&pico_mq->lock);
            OSAL_LOG_DBG("Message received from queue (count now %u)\r\n", pico_mq->count);
            return 0;
        }

        critical_section_exit(&pico_mq->lock);

        if (timeout != USB_OSAL_WAITING_FOREVER && absolute_time_diff_us(get_absolute_time(), timeout_time) <= 0)
        {
            OSAL_LOG_DBG("Message queue receive timeout\r\n");
            return -USB_ERR_TIMEOUT;
        }

        // Small delay to prevent busy waiting
        sleep_us(CONFIG_USB_OSAL_POLL_INTERVAL_US);
    }
}

/* ========================================================================= */
/* TIMER FUNCTIONS */
/* ========================================================================= */

struct usb_osal_timer *usb_osal_timer_create(const char *name, uint32_t timeout_ms, usb_timer_handler_t handler, void *argument, bool is_period)
{
    ensure_osal_initialized();

    pico_timer_node_t *node = (pico_timer_node_t *)malloc(sizeof(pico_timer_node_t));
    if (node == NULL)
    {
        OSAL_LOG_ERR("Create timer %s failed - no memory\r\n", name ? name : "unnamed");
        return NULL;
    }

    memset(node, 0, sizeof(pico_timer_node_t));
    node->timer.handler = handler;
    node->timer.argument = argument;
    node->timer.is_period = is_period;
    node->timer.timeout_ms = timeout_ms;
    node->active = false;

    // Add to timer list
    critical_section_enter_blocking(&timer_lock);
    node->next = timer_list;
    timer_list = node;
    critical_section_exit(&timer_lock);

    OSAL_LOG_DBG("Created timer %s (timeout=%ums, periodic=%s)\r\n",
                 name ? name : "unnamed", timeout_ms, is_period ? "yes" : "no");
    return &node->timer;
}

void usb_osal_timer_delete(struct usb_osal_timer *timer)
{
    if (timer == NULL)
        return;

    pico_timer_node_t *node = (pico_timer_node_t *)((char *)timer - offsetof(pico_timer_node_t, timer));

    // Stop timer if running
    if (node->active)
    {
        cancel_repeating_timer(&node->rpt_timer);
        node->active = false;
    }

    // Remove from timer list
    critical_section_enter_blocking(&timer_lock);
    if (timer_list == node)
    {
        timer_list = node->next;
    }
    else
    {
        pico_timer_node_t *current = timer_list;
        while (current && current->next != node)
        {
            current = current->next;
        }
        if (current)
        {
            current->next = node->next;
        }
    }
    critical_section_exit(&timer_lock);

    free(node);
    OSAL_LOG_DBG("Deleted timer\r\n");
}

void usb_osal_timer_start(struct usb_osal_timer *timer)
{
    if (timer == NULL)
        return;

    pico_timer_node_t *node = (pico_timer_node_t *)((char *)timer - offsetof(pico_timer_node_t, timer));

    if (node->active)
    {
        cancel_repeating_timer(&node->rpt_timer);
    }

    node->rpt_timer.user_data = node;

    if (timer->is_period)
    {
        // Periodic timer
        add_repeating_timer_ms(-(int32_t)timer->timeout_ms, timer_callback, node, &node->rpt_timer);
        OSAL_LOG_DBG("Started periodic timer (%ums)\r\n", timer->timeout_ms);
    }
    else
    {
        // One-shot timer - simulate with repeating timer that cancels itself
        add_repeating_timer_ms(-(int32_t)timer->timeout_ms, timer_callback, node, &node->rpt_timer);
        OSAL_LOG_DBG("Started one-shot timer (%ums)\r\n", timer->timeout_ms);
    }

    node->active = true;
}

void usb_osal_timer_stop(struct usb_osal_timer *timer)
{
    if (timer == NULL)
        return;

    pico_timer_node_t *node = (pico_timer_node_t *)((char *)timer - offsetof(pico_timer_node_t, timer));

    if (node->active)
    {
        cancel_repeating_timer(&node->rpt_timer);
        node->active = false;
        OSAL_LOG_DBG("Stopped timer\r\n");
    }
}

/* ========================================================================= */
/* CRITICAL SECTION FUNCTIONS */
/* ========================================================================= */

size_t usb_osal_enter_critical_section(void)
{
    return save_and_disable_interrupts();
}

void usb_osal_leave_critical_section(size_t flag)
{
    restore_interrupts(flag);
}

/* ========================================================================= */
/* SLEEP FUNCTION */
/* ========================================================================= */

void usb_osal_msleep(uint32_t delay)
{
    sleep_ms(delay);
}

/* ========================================================================= */
/* MEMORY FUNCTIONS */
/* ========================================================================= */

void *usb_osal_malloc(size_t size)
{
    return malloc(size);
}

void usb_osal_free(void *ptr)
{
    free(ptr);
}
