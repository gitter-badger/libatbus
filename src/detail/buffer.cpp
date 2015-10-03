//
// Created by 欧文韬 on 2015/8/11.
//

#include <cstdlib>
#include <cstring>
#include <assert.h>

#include "detail/libatbus_error.h"
#include "detail/buffer.h"


namespace atbus {
    namespace detail {

        namespace fn {
            void* buffer_step(void* pointer, size_t step) {
                return reinterpret_cast<char*>(pointer) + step;
            }

            const void* buffer_step(const void* pointer, size_t step) {
                return reinterpret_cast<const char*>(pointer) + step;
            }

            size_t buffer_offset(const void* l, const void* r) {
                const char* lc = reinterpret_cast<const char*>(l);
                const char* rc = reinterpret_cast<const char*>(r);
                return lc < rc? rc - lc: lc - rc;
            }

            size_t read_vint(uint64_t& out, void* pointer, size_t s) {
                out = 0;

                if (s == 0 || NULL == pointer) {
                    return 0;
                }

                size_t left = s;
                for(char* d = reinterpret_cast<char*>(pointer); left > 0; ++ d) {
                    -- left;

                    out <<= 7;
                    out |= 0x7F & *d;

                    if (0 == (0x80 & *d)) {
                        break;
                    } else if (0 == left) {
                        return 0;
                    }
                }

                return s - left;
            }

            size_t write_vint(uint64_t in, void* pointer, size_t s) {
                if (s == 0 || NULL == pointer) {
                    return 0;
                }

                size_t used = 1;
                char* d = reinterpret_cast<char*>(pointer);
                *d = 0x7F & in;
                in >>= 7;

                while (in && used + 1 <= s) {
                    ++ used;
                    ++ d;

                    *d = 0x80 | (in & 0x7F);
                    in >>= 7;
                }

                if (in) {
                    return 0;
                }

                char* ss = reinterpret_cast<char*>(pointer);
                if (ss < d) {
                    std::reverse(ss, d + 1);
                }

                return used;
            }
        }

        void* buffer_block::data() {
            return fn::buffer_step(pointer_, used_);
        }

        const void* buffer_block::data() const {
            return fn::buffer_step(pointer_, used_);
        }

        const void* buffer_block::raw_data() const {
            return pointer_;
        }

        size_t buffer_block::size() const {
            return size_ - used_;
        }

        size_t buffer_block::raw_size() const {
            return  size_;
        }

        void* buffer_block::pop(size_t s) {
            if (used_ + s > size_) {
                used_ = size_;
            } else {
                used_ += s;
            }

            return data();
        }

        size_t buffer_block::instance_size() const {
            return head_size(size_);
        }

        /** alloc and init buffer_block **/
        buffer_block* buffer_block::malloc(size_t s) {
            size_t ms = full_size(s);

            void* ret = ::malloc(ms);
            if (NULL != ret) {
                create(ret, ms, s);
            }

            return reinterpret_cast<buffer_block*>(ret);
        }

        /** destroy and free buffer_block **/
        void buffer_block::free(buffer_block* p) {
            if (NULL != p) {
                destroy(p);
                ::free(p);
            }
        }

        /** init buffer_block as specify address **/
        void* buffer_block::create(void* pointer, size_t s, size_t bs) {
            if (NULL == pointer) {
                return NULL;
            }

            size_t fs = full_size(bs);
            size_t hs = head_size(bs);
            if (fs > s) {
                return NULL;
            }

            char* addr = reinterpret_cast<char*>(pointer);
            buffer_block* res = reinterpret_cast<buffer_block*>(pointer);
            res->size_ = bs;
            res->pointer_ = addr + hs;
            res->used_ = 0;

            return addr + fs;
        }

        /** init buffer_block as specify address **/
        void* buffer_block::destroy(buffer_block* p) {
            if (NULL == p) {
                return NULL;
            }

            return fn::buffer_step(p->pointer_, p->size_);
        }

        size_t buffer_block::padding_size(size_t s) {
            size_t pl = s % sizeof(size_t);
            if (0 == pl) {
                return s;
            }

            return s + sizeof(size_t) - pl;
        }

        size_t buffer_block::head_size(size_t s) {
            return padding_size(sizeof(buffer_block));
        }

        size_t buffer_block::full_size(size_t s) {
            return head_size(s) + padding_size(s);
        }

        // ================= buffer manager =================
        buffer_manager::buffer_manager() {
            static_buffer_.buffer_ = NULL;

            reset();
        }

        buffer_manager::~buffer_manager() {
            reset();
        }

        const buffer_manager::limit_t& buffer_manager::limit() const {
            return limit_;
        }

        bool buffer_manager::set_limit(size_t max_size, size_t max_number) {
            if (NULL == static_buffer_.buffer_) {
                limit_.limit_number_ = max_number;
                limit_.limit_size_ = max_size;
                return true;
            }

            return false;
        }

        int buffer_manager::front(void*& pointer, size_t& s) {
            return NULL == static_buffer_.buffer_? dynamic_front(pointer, s): static_front(pointer, s);
        }

        int buffer_manager::back(void*& pointer, size_t& s) {
            return NULL == static_buffer_.buffer_? dynamic_back(pointer, s): static_back(pointer, s);
        }

        int buffer_manager::push_back(void*& pointer, size_t s) {
            pointer = NULL;
            if (limit_.limit_number_ > 0 && limit_.cost_number_ >= limit_.limit_number_) {
                return EN_ATBUS_ERR_BUFF_LIMIT;
            }

            if (limit_.limit_size_ > 0 && limit_.cost_size_ + s > limit_.limit_size_) {
                return EN_ATBUS_ERR_BUFF_LIMIT;
            }

            int res = NULL == static_buffer_.buffer_? dynamic_push_back(pointer, s): static_push_back(pointer, s);
            if (res >= 0) {
                ++ limit_.cost_number_;
                limit_.cost_size_ += s;
            }

            return res;
        }

        int buffer_manager::push_front(void*& pointer, size_t s) {
            pointer = NULL;
            if (limit_.limit_number_ > 0 && limit_.cost_number_ >= limit_.limit_number_) {
                return EN_ATBUS_ERR_BUFF_LIMIT;
            }

            if (limit_.limit_size_ > 0 && limit_.cost_size_ + s > limit_.limit_size_) {
                return EN_ATBUS_ERR_BUFF_LIMIT;
            }

            int res = NULL == static_buffer_.buffer_? dynamic_push_front(pointer, s): static_push_front(pointer, s);
            if (res >= 0) {
                ++ limit_.cost_number_;
                limit_.cost_size_ += s;
            }

            return res;
        }

        int buffer_manager::pop_back(size_t s) {
            return NULL == static_buffer_.buffer_? dynamic_pop_back(s): static_pop_back(s);
        }

        int buffer_manager::pop_front(size_t s) {
            return NULL == static_buffer_.buffer_? dynamic_pop_front(s): static_pop_front(s);
        }

        bool buffer_manager::empty() const {
            return NULL == static_buffer_.buffer_? dynamic_empty(): static_empty();
        }

        int buffer_manager::static_front(void*& pointer, size_t& s) {
            if (static_empty()) {
                pointer = NULL;
                s = 0;
                return EN_ATBUS_ERR_NO_DATA;
            }

            buffer_block& t = *static_buffer_.circle_index_[static_buffer_.head_];
            pointer = t.data();
            s = t.size();

            return EN_ATBUS_ERR_SUCCESS;
        }

        int buffer_manager::static_back(void*& pointer, size_t& s) {
            if (static_empty()) {
                pointer = NULL;
                s = 0;
                return EN_ATBUS_ERR_NO_DATA;
            }

            buffer_block& t = *static_buffer_.circle_index_[
                (static_buffer_.tail_ + static_buffer_.circle_index_.size() - 1) % static_buffer_.circle_index_.size()
            ];
            pointer = t.data();
            s = t.size();

            return EN_ATBUS_ERR_SUCCESS;
        }

        int buffer_manager::static_push_back(void*& pointer, size_t s) {
            assert(static_buffer_.circle_index_.size() >= 2);

            pointer = NULL;
            if ((static_buffer_.tail_ + 1) % static_buffer_.circle_index_.size() == static_buffer_.head_) {
                return EN_ATBUS_ERR_BUFF_LIMIT;
            }

#define assign_tail(x) static_buffer_.circle_index_[static_buffer_.tail_] = reinterpret_cast<buffer_block*>(x)
#define add_tail() \
            pointer = static_buffer_.circle_index_[static_buffer_.tail_]->data(); \
            static_buffer_.tail_ = (static_buffer_.tail_ + 1) % static_buffer_.circle_index_.size()

            buffer_block* head = static_buffer_.circle_index_[static_buffer_.head_];
            buffer_block* tail = static_buffer_.circle_index_[static_buffer_.tail_];

            size_t fs = buffer_block::full_size(s);
            // empty init
            if (NULL == head || NULL == tail) {
                static_buffer_.tail_ = 0;
                static_buffer_.head_ = 0;
                assign_tail(static_buffer_.buffer_);

                head = static_buffer_.circle_index_[static_buffer_.head_];
                tail = static_buffer_.circle_index_[static_buffer_.tail_];
            }

            if (tail >= head) { // .... head NNNNNN tail ....
                size_t free_len = fn::buffer_offset(tail, fn::buffer_step(static_buffer_.buffer_, static_buffer_.size_));

                if (free_len >= fs) { // .... head NNNNNN old_tail NN new_tail ....
                    void* next_free = buffer_block::create(tail, free_len, s);
                    if (NULL == next_free) {
                        return EN_ATBUS_ERR_MALLOC;
                    }
                    assert(fn::buffer_step(static_buffer_.buffer_, static_buffer_.size_) >= next_free);

                    add_tail();
                    assign_tail(next_free);
                } else { // NN new_tail ... head NNNNNN old_tail ....
                    free_len = fn::buffer_offset(static_buffer_.buffer_, head);
                    if (free_len < fs) {
                        return EN_ATBUS_ERR_BUFF_LIMIT;
                    }

                    void* next_free = buffer_block::create(static_buffer_.buffer_, free_len, s);
                    if (NULL == next_free) {
                        return EN_ATBUS_ERR_MALLOC;
                    }
                    assert(next_free <= head);

                    assign_tail(static_buffer_.buffer_);
                    add_tail();
                    assign_tail(next_free);
                }
            } else { // NNN tail ....  head NNNNNN ....
                size_t free_len = fn::buffer_offset(tail, head);
                if (free_len < fs) {
                    return EN_ATBUS_ERR_BUFF_LIMIT;
                }

                // NNN old_tail NN new_tail ....  head NNNNNN ....
                void* next_free = buffer_block::create(tail, free_len, s);
                if (NULL == next_free) {
                    return EN_ATBUS_ERR_MALLOC;
                }
                assert(next_free <= head);

                add_tail();
                assign_tail(next_free);
            }

#undef add_tail
#undef assign_tail

            return EN_ATBUS_ERR_SUCCESS;
        }

        int buffer_manager::static_push_front(void*& pointer, size_t s) {
            assert(static_buffer_.circle_index_.size() >= 2);

            pointer = NULL;
            if ((static_buffer_.tail_ + 1) % static_buffer_.circle_index_.size() == static_buffer_.head_) {
                return EN_ATBUS_ERR_BUFF_LIMIT;
            }

#define index_pre_head() ((static_buffer_.head_ + static_buffer_.circle_index_.size() - 1) % static_buffer_.circle_index_.size())
#define assign_head(x) static_buffer_.circle_index_[static_buffer_.head_] = reinterpret_cast<buffer_block*>(x)
#define sub_head(d) \
            static_buffer_.head_ = index_pre_head();                                \
            assign_head(d);                                                         \
            pointer = static_buffer_.circle_index_[static_buffer_.head_]->data()

            buffer_block* head = static_buffer_.circle_index_[static_buffer_.head_];
            buffer_block* tail = static_buffer_.circle_index_[static_buffer_.tail_];

            size_t fs = buffer_block::full_size(s);
            // empty init
            if (NULL == head || NULL == tail) {
                static_buffer_.tail_ = 0;
                static_buffer_.head_ = 0;
                assign_head(static_buffer_.buffer_);

                head = static_buffer_.circle_index_[static_buffer_.head_];
                tail = static_buffer_.circle_index_[static_buffer_.tail_];
            }

            if (tail >= head) { // .... head NNNNNN tail ....
                size_t free_len = fn::buffer_offset(head, static_buffer_.buffer_);
                if (free_len >= fs) { // .... new_head NN old_head NNNNNN tail ....
                    void* buffer_start = fn::buffer_step(static_buffer_.buffer_, free_len - fs);
                    void* next_free = buffer_block::create(buffer_start, fs, s);
                    if (NULL == next_free) {
                        return EN_ATBUS_ERR_MALLOC;
                    }
                    assert(head == next_free);
                    sub_head(buffer_start);

                } else { // ... old_head NNNNNN tail .... new_head NN
                    free_len = fn::buffer_offset(tail, fn::buffer_step(static_buffer_.buffer_, static_buffer_.size_));
                    if (free_len < fs) {
                        return EN_ATBUS_ERR_BUFF_LIMIT;
                    }

                    void* buffer_start = fn::buffer_step(tail, free_len - fs);
                    void* next_free = buffer_block::create(buffer_start, fs, s);
                    if (NULL == next_free) {
                        return EN_ATBUS_ERR_MALLOC;
                    }
                    assert(next_free == fn::buffer_step(static_buffer_.buffer_, static_buffer_.size_));
                    sub_head(buffer_start);
                }

            } else { // NNN tail ....  head NNNNNN ....
                size_t free_len = fn::buffer_offset(tail, head);
                if (free_len < fs) {
                    return EN_ATBUS_ERR_BUFF_LIMIT;
                }

                void* buffer_start = fn::buffer_step(tail, free_len - fs);
                // NNN tail  .... new_head NN head NNNNNN ....
                void* next_free = buffer_block::create(buffer_start, fs, s);
                if (NULL == next_free) {
                    return EN_ATBUS_ERR_MALLOC;
                }
                assert(next_free == head);
                sub_head(buffer_start);
            }

#undef sub_head
#undef assign_head
#undef index_pre_head
            return EN_ATBUS_ERR_SUCCESS;
        }

        int buffer_manager::static_pop_back(size_t s) {
            if (static_empty()) {
                return EN_ATBUS_ERR_NO_DATA;
            }

#define index_tail() ((static_buffer_.tail_ + static_buffer_.circle_index_.size() - 1) % static_buffer_.circle_index_.size())
#define assign_tail(x) static_buffer_.circle_index_[index_tail()] = reinterpret_cast<buffer_block*>(x)
#define sub_tail(x) static_buffer_.tail_ = x

            size_t tail_index = index_tail();
            buffer_block* tail = static_buffer_.circle_index_[tail_index];

            if (s > tail->size()) {
                s = tail->size();
            }

            tail->pop(s);
            if (0 == tail->size()) {
                buffer_block::destroy(tail);
                assign_tail(NULL);
                sub_tail(tail_index);

                if (limit_.cost_number_ > 0) {
                    --limit_.cost_number_;
                }
            }


            // fix limit and reset to init state
            if (static_empty()) {
                static_buffer_.head_ = 0;
                static_buffer_.tail_ = 0;
                static_buffer_.circle_index_[static_buffer_.tail_] = reinterpret_cast<buffer_block*>(static_buffer_.buffer_);

                limit_.cost_size_ = 0;
                limit_.cost_number_ = 0;
            } else {
                limit_.cost_size_ -= limit_.cost_size_ >= s? s: limit_.cost_size_;
            }

#undef assign_tail
#undef sub_tail
#undef index_tail
            return EN_ATBUS_ERR_SUCCESS;
        }

        int buffer_manager::static_pop_front(size_t s) {
            if (static_empty()) {
                return EN_ATBUS_ERR_NO_DATA;
            }

#define assign_head(x) static_buffer_.circle_index_[static_buffer_.head_] = reinterpret_cast<buffer_block*>(x)
#define add_head() static_buffer_.head_ = (static_buffer_.head_ + 1) % static_buffer_.circle_index_.size()

            buffer_block* head = static_buffer_.circle_index_[static_buffer_.head_];

            if (s > head->size()) {
                s = head->size();
            }

            head->pop(s);
            if (0 == head->size()) {
                buffer_block::destroy(head);
                assign_head(NULL);
                add_head();

                if (limit_.cost_number_ > 0) {
                    --limit_.cost_number_;
                }
            }


            // fix limit and reset to init state
            if (static_empty()) {
                static_buffer_.head_ = 0;
                static_buffer_.tail_ = 0;
                static_buffer_.circle_index_[static_buffer_.tail_] = reinterpret_cast<buffer_block*>(static_buffer_.buffer_);

                limit_.cost_size_ = 0;
                limit_.cost_number_ = 0;
            } else {
                limit_.cost_size_ -= limit_.cost_size_ >= s? s: limit_.cost_size_;
            }

#undef assign_head
#undef add_head
            return EN_ATBUS_ERR_SUCCESS;
        }

        bool buffer_manager::static_empty() const {
            return static_buffer_.head_ == static_buffer_.tail_;
        }


        int buffer_manager::dynamic_front(void*& pointer, size_t& s) {
            if (dynamic_empty()) {
                pointer = NULL;
                s = 0;
                return EN_ATBUS_ERR_NO_DATA;
            }

            buffer_block& t = *dynamic_buffer_.front();
            pointer = t.data();
            s = t.size();

            return EN_ATBUS_ERR_SUCCESS;
        }

        int buffer_manager::dynamic_back(void*& pointer, size_t& s) {
            if (dynamic_empty()) {
                pointer = NULL;
                s = 0;
                return EN_ATBUS_ERR_NO_DATA;
            }

            buffer_block& t = *dynamic_buffer_.back();
            pointer = t.data();
            s = t.size();

            return EN_ATBUS_ERR_SUCCESS;
        }

        int buffer_manager::dynamic_push_back(void*& pointer, size_t s) {
            buffer_block* res = buffer_block::malloc(s);
            if (NULL == res) {
                pointer = NULL;
                return EN_ATBUS_ERR_MALLOC;
            }

            dynamic_buffer_.push_back(res);
            pointer = res->data();

            return EN_ATBUS_ERR_SUCCESS;
        }

        int buffer_manager::dynamic_push_front(void*& pointer, size_t s) {
            buffer_block* res = buffer_block::malloc(s);
            if (NULL == res) {
                pointer = NULL;
                return EN_ATBUS_ERR_MALLOC;
            }

            dynamic_buffer_.push_front(res);
            pointer = res->data();

            return EN_ATBUS_ERR_SUCCESS;
        }

        int buffer_manager::dynamic_pop_back(size_t s) {
            if (dynamic_empty()) {
                return EN_ATBUS_ERR_NO_DATA;
            }

            buffer_block* t = dynamic_buffer_.back();
            if (s > t->size()) {
                s = t->size();
            }

            t->pop(s);
            if(t->size() <= 0) {
                buffer_block::free(t);
                dynamic_buffer_.pop_back();

                if (limit_.cost_number_ > 0) {
                    --limit_.cost_number_;
                }
            }

            // fix limit
            if (dynamic_empty()) {
                limit_.cost_size_ = 0;
                limit_.cost_number_ = 0;
            } else {
                limit_.cost_size_ -= limit_.cost_size_ >= s? s: limit_.cost_size_;
            }

            return EN_ATBUS_ERR_SUCCESS;
        }

        int buffer_manager::dynamic_pop_front(size_t s) {
            if (dynamic_empty()) {
                return EN_ATBUS_ERR_NO_DATA;
            }

            buffer_block* t = dynamic_buffer_.front();
            if (s > t->size()) {
                s = t->size();
            }

            t->pop(s);
            if(t->size() <= 0) {
                buffer_block::free(t);
                dynamic_buffer_.pop_front();

                if (limit_.cost_number_ > 0) {
                    --limit_.cost_number_;
                }
            }

            // fix limit
            if (dynamic_empty()) {
                limit_.cost_size_ = 0;
                limit_.cost_number_ = 0;
            } else {
                limit_.cost_size_ -= limit_.cost_size_ >= s? s: limit_.cost_size_;
            }

            return EN_ATBUS_ERR_SUCCESS;
        }

        bool buffer_manager::dynamic_empty() const {
            return dynamic_buffer_.empty();
        }

        void buffer_manager::reset() {
            static_buffer_.head_ = 0;
            static_buffer_.tail_ = 0;
            static_buffer_.size_ = 0;
            static_buffer_.circle_index_.clear();
            if (NULL != static_buffer_.buffer_) {
                ::free(static_buffer_.buffer_);
                static_buffer_.buffer_ = NULL;
            }

            // dynamic buffers
            while(!dynamic_buffer_.empty()) {
                buffer_block::free(dynamic_buffer_.front());
                dynamic_buffer_.pop_front();
            }

            limit_.cost_size_ = 0;
            limit_.cost_number_ = 0;
            limit_.limit_number_ = 0;
            limit_.limit_size_ = 0;
        }

        void buffer_manager::set_mode(size_t max_size, size_t max_number) {
            reset();

            if (0 != max_size && max_number > 0) {
                size_t bfs = buffer_block::padding_size(max_size);
                static_buffer_.buffer_ = ::malloc(bfs);
                if (NULL != static_buffer_.buffer_) {
                    static_buffer_.size_ = bfs;

                    // left 1 empty bound
                    static_buffer_.circle_index_.resize(max_number + 1, NULL);
                    limit_.limit_size_ = max_size;
                    limit_.limit_number_ = max_number;
                }
            }
        }

    }
}
