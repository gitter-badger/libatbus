//
// Created by Å·ÎÄèº on 2015/8/11.
//

#ifndef LIBATBUS_BUFFER_H
#define LIBATBUS_BUFFER_H

#include <algorithm>
#include <list>
#include <vector>
#include <stdint.h>

#if defined(__cplusplus) && __cplusplus >= 201103L
#include <type_traits>
#endif

namespace atbus {
    namespace detail {
        namespace fn {
            void* buffer_step(void* pointer, size_t step);
            const void* buffer_step(const void* pointer, size_t step);

            size_t buffer_offset(const void* l, const void* r);

            /**
             * @brief try to read a dynamic int from buffer
             * @param out output integer
             * @param pointer buffer address
             * @param s buffer size
             * @note encoding: like protobuf varint, first bit means more or last byte, big endian, padding right
             * @note can not used with signed integer
             * @return how much bytes the integer cost, 0 if failed
             **/
            size_t read_vint(uint64_t& out, void* pointer, size_t s);

            /**
             * @brief try to write a dynamic int to buffer
             * @param in input integer
             * @param pointer buffer address
             * @param s buffer size
             * @note encoding: like protobuf varint, first bit means more or last byte, big endian, padding right
             * @note can not used with signed integer
             * @return how much bytes the integer cost, 0 if failed
             **/
            size_t write_vint(uint64_t in, void* pointer, size_t s);
        }

        /**
         * @brief buffer block, not thread safe
         */
        class buffer_block {
        public:
            void* data();
            const void* data() const;
            const void* raw_data() const;

            size_t size() const;

            size_t raw_size() const;

            void* pop(size_t s);

            size_t instance_size() const;
        public:
            /** alloc and init buffer_block **/
            static buffer_block* malloc(size_t s);

            /** destroy and free buffer_block **/
            static void free(buffer_block* p);

            /**
             * @brief init buffer_block as specify address
             * @param pointer data address
             * @param s data max size
             * @param bs buffer size
             * @return unused data address
             **/
            static void* create(void* pointer, size_t s, size_t bs);

            /** init buffer_block as specify address **/
            static void* destroy(buffer_block* p);

            static size_t padding_size(size_t s);
            static size_t head_size(size_t s);
            static size_t full_size(size_t s);
        private:
            size_t size_;
            size_t used_;
            void* pointer_;
        };

#if defined(__cplusplus) && __cplusplus >= 201103L
        static_assert(std::is_pod<buffer_block>::value, "buffer_block must be pod");
#endif

        /**
         * @brief buffer block manager, not thread safe
         */
        class buffer_manager {
        public:
            struct limit_t {
                size_t cost_number_;
                size_t cost_size_;

                size_t limit_number_;
                size_t limit_size_;
            };

        private:
            buffer_manager(const buffer_manager&);
            buffer_manager& operator=(const buffer_manager&);

        public:
            buffer_manager();
            ~buffer_manager();

            const limit_t& limit() const;

            /**
             * @brief set limit when in dynamic mode
             * @param max_size size limit of dynamic, set 0 if unlimited
             * @param max_number number limit of dynamic, set 0 if unlimited
             * @return true on success
             */
            bool set_limit(size_t max_size, size_t max_number);

            int front(void*& pointer, size_t& s);

            int back(void*& pointer, size_t& s);

            int push_back(void*& pointer, size_t s);

            int push_front(void*& pointer, size_t s);

            int pop_back(size_t s);

            int pop_front(size_t s);

            bool empty() const;

            void reset();

            /**
             * @brief set dynamic mode(use malloc when push buffer) or static mode(malloc a huge buffer at once)
             * @param max_size circle buffer size when static mode, 0 when dynamic mode
             * @param max_number buffer number when static mode
             * @note this api will clear buffer data already exists
             */
            void set_mode(size_t max_size, size_t max_number);
        private:
            int static_front(void*& pointer, size_t& s);

            int static_back(void*& pointer, size_t& s);

            int static_push_back(void*& pointer, size_t s);

            int static_push_front(void*& pointer, size_t s);

            int static_pop_back(size_t s);

            int static_pop_front(size_t s);

            bool static_empty() const;

            int dynamic_front(void*& pointer, size_t& s);

            int dynamic_back(void*& pointer, size_t& s);

            int dynamic_push_back(void*& pointer, size_t s);

            int dynamic_push_front(void*& pointer, size_t s);

            int dynamic_pop_back(size_t s);

            int dynamic_pop_front(size_t s);

            bool dynamic_empty() const;

        private:
            struct static_buffer_t {
                void* buffer_;
                size_t size_;

                size_t head_;
                size_t tail_;
                std::vector<buffer_block*> circle_index_;
            };

            static_buffer_t static_buffer_;
            std::list<buffer_block*> dynamic_buffer_;

            limit_t limit_;
        };
    }
}

#endif //LIBATBUS_BUFFER_H
