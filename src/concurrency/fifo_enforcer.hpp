#ifndef __CONCURRENCY_FIFO_ENFORCER_HPP__
#define __CONCURRENCY_FIFO_ENFORCER_HPP__

#include <map>

#include "concurrency/mutex_assertion.hpp"
#include "timestamps.hpp"
#include "utils.hpp"

class cond_t;
class signal_t;

/* `fifo_enforcer.hpp` contains facilities for enforcing that objects pass
through a checkpoint in the same order that they passed through a previous
checkpoint. The objects in transit between the checkpoints are identified by
"tokens", of the types `fifo_enforcer_read_token_t` and
`fifo_enforcer_write_token_t`. Read tokens are allowed to be reordered relative
to each other but not relative to write tokens. */

class fifo_enforcer_read_token_t {
public:
    fifo_enforcer_read_token_t() THROWS_NOTHING { }
private:
    friend class fifo_enforcer_source_t;
    friend class fifo_enforcer_sink_t;
    fifo_enforcer_read_token_t(state_timestamp_t t) THROWS_NOTHING :
        timestamp(t) { }
    state_timestamp_t timestamp;
};

class fifo_enforcer_write_token_t {
public:
    fifo_enforcer_write_token_t() THROWS_NOTHING { }
private:
    friend class fifo_enforcer_source_t;
    friend class fifo_enforcer_sink_t;
    fifo_enforcer_write_token_t(transition_timestamp_t t, int npr) THROWS_NOTHING :
        timestamp(t), num_preceding_reads(npr) { }
    transition_timestamp_t timestamp;
    int num_preceding_reads;
};

class fifo_enforcer_source_t : public home_thread_mixin_t {
public:
    /* `state_t` represents the internal state of a `fifo_enforcer_source_t`. If
    you want to initialize a `fifo_enforcer_sink_t` in such a way that it
    "skips" all of the tokens that have already been generated by the
    `fifo_enforcer_source_t`, then call `get_state()` on the
    `fifo_enforcer_source_t` and pass the result to the `fifo_enforcer_sink_t`
    constructor. */
    class state_t {
    public:
        state_t() THROWS_NOTHING { }
    private:
        friend class fifo_enforcer_source_t;
        friend class fifo_enforcer_sink_t;
        state_t(state_timestamp_t ts, int nr) THROWS_NOTHING : timestamp(ts), num_reads(nr) { }
        state_timestamp_t timestamp;
        int num_reads;
    };

    fifo_enforcer_source_t() THROWS_NOTHING :
        state(state_timestamp_t::zero(), 0) { }

    /* Enters the FIFO for read. Does not block. */
    fifo_enforcer_read_token_t enter_read() THROWS_NOTHING;

    /* Enters the FIFO for write. Does not block. */
    fifo_enforcer_write_token_t enter_write() THROWS_NOTHING;

    state_t get_state() THROWS_NOTHING {
        return state;
    }

private:
    mutex_assertion_t lock;
    state_t state;
    DISABLE_COPYING(fifo_enforcer_source_t);
};

class fifo_enforcer_sink_t : public home_thread_mixin_t {
public:
    /* To avoid race conditions immediately after exiting the FIFO, exiting
    is implemented as a sentry-object rather than a method. The constructor for
    an `exit_read_t` or `exit_write_t` will block until the given token is
    allowed to exit the FIFO. Higher-numbered tokens will not be allowed to
    proceed until after the `exit_read_t` or `exit_write_t` has been destroyed.
    */

    /* If the `interruptor` parameter to `exit_read_t::exit_read_t()` or
    `exit_write_t::exit_write_t()` is pulsed, then it will throw
    `interrupted_exc_t` immediately, leaving the `fifo_enforcer_sink_t` in the
    same state as if the interrupted token had never arrived. */

    class exit_read_t {
    public:
        exit_read_t() THROWS_NOTHING;
        exit_read_t(fifo_enforcer_sink_t *, fifo_enforcer_read_token_t, signal_t *interruptor) THROWS_ONLY(interrupted_exc_t);
        ~exit_read_t() THROWS_NOTHING;
        void reset() THROWS_NOTHING;
        void reset(fifo_enforcer_sink_t *, fifo_enforcer_read_token_t, signal_t *interruptor) THROWS_ONLY(interrupted_exc_t);
    private:
        fifo_enforcer_sink_t *parent;
        fifo_enforcer_read_token_t token;
    };

    class exit_write_t {
    public:
        exit_write_t() THROWS_NOTHING;
        exit_write_t(fifo_enforcer_sink_t *, fifo_enforcer_write_token_t, signal_t *interruptor) THROWS_ONLY(interrupted_exc_t);
        ~exit_write_t() THROWS_NOTHING;
        void reset() THROWS_NOTHING;
        void reset(fifo_enforcer_sink_t *, fifo_enforcer_write_token_t, signal_t *interruptor) THROWS_ONLY(interrupted_exc_t);
    private:
        fifo_enforcer_sink_t *parent;
        fifo_enforcer_write_token_t token;
    };

    fifo_enforcer_sink_t() THROWS_NOTHING :
        state(state_timestamp_t::zero(), 0) { }

    explicit fifo_enforcer_sink_t(fifo_enforcer_source_t::state_t init) THROWS_NOTHING :
        state(init) { }

    ~fifo_enforcer_sink_t() THROWS_NOTHING {
        rassert(waiting_readers.empty());
        rassert(waiting_writers.empty());
    }

private:
    void pump_readers() THROWS_NOTHING;
    void pump_writers() THROWS_NOTHING;
    mutex_assertion_t lock;
    fifo_enforcer_source_t::state_t state;
    std::multimap<state_timestamp_t, cond_t *> waiting_readers;
    std::map<transition_timestamp_t, std::pair<int, cond_t *> > waiting_writers;
    DISABLE_COPYING(fifo_enforcer_sink_t);
};

#endif /* __CONCURRENCY_FIFO_ENFORCER_HPP__ */
