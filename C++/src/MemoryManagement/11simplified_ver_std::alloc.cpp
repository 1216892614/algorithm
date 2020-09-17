#include <iostream>
#include <algorithm>
#include <cstring>

using namespace std;
/*
    一级分配器
*/
class alloc_pri{
    public:
        static void* allocate(size_t size) {

            void* result = malloc(size); 
            return result;
        }

        static void deallocate(void* p, size_t) {
        
            free(p); 
        }

};
typedef  alloc_pri malloc_alloc;

/*
    二级分配器
*/
class alloc_default{

    private:
        //链表指标
        static const int _align = 8;
        static const int _max = 128;
        static const int _list = _max / _align;
        //嵌入式指针
        union obj{
            union obj* next;
        };

    private:
        //链表元素
        static obj* free_list[_list];
        static char* start_free;
        static char* end_free;
        static size_t heap_size;

        //工具函数
        static size_t FREELIST_INDEX(size_t bytes);
        static size_t ROUND_UP(size_t bytes);

        //核心函数
        static void* refill(size_t size);
        static char* chunk_alloc(size_t, int &);
    public:
        //分配函数
        static void* allocate(size_t);
        static void deallocate(void*, size_t);
        static void realloc(void* p, size_t, size_t);
};

/*类静态变量定义*/
char* alloc_default::start_free = 0;
char* alloc_default::end_free = 0;
size_t alloc_default::heap_size = 0;
alloc_default::obj* alloc_default::free_list[_list]
    = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};  //16 chunks


/************分配函数实现
    和6per_class_allocator的分配算法完全一样
 */
void* alloc_default::allocate(size_t size) {
    obj** my_free_list;   //指向内存池链表的的指针
    obj* result;

    //当需要申请的size大于allocator的最大chunk的时候， 直接调用一级分配器————>malloc()
    if(size > _max)  
        return (malloc_alloc::allocate(size));

    //根据申请的size判断内存池相应的指针端口
    my_free_list = free_list + FREELIST_INDEX(size);  
    result = *my_free_list;

    //如果最合适的指针端口已经没有内存了，那就需要向上找指针端口进行分配(80-88-96...)
    if(!result) {
        void* t = refill(ROUND_UP(size)); 
        return t;
    }

    //如果有可用区块，分配
    *my_free_list = result->next;
    return result;
}
void alloc_default::deallocate(void* p, size_t size) {

    obj* q = static_cast<obj*>(p);

    //当需要销毁的size大于deallocator的最大chunk的时候， 直接调用一级分配器————>free()
    if(size > _max) {
        malloc_alloc::deallocate(p, size); 
        return;
    }

    obj** my_free_list = free_list + FREELIST_INDEX(size);
    q->next = *my_free_list; 
    *my_free_list = q;
}


/**************工具函数实现
    纯粹数学计算/理解向上取整
*/
size_t alloc_default::FREELIST_INDEX(size_t bytes) {
    return ((bytes + _align - 1) / _align - 1);
}
size_t alloc_default::ROUND_UP(size_t bytes) {
    return (bytes + _align) & ~(_align - 1);
}


/********************核心函数
    @功能： 
 
*/
void* alloc_default::refill(size_t size) {

    int nobjs = 20;
    char* chunk = chunk_alloc(size, nobjs);
    if(1 == nobjs) return(chunk); 

    //定位
    obj** my_free_list = free_list + FREELIST_INDEX(size);
    obj* result = (obj*)chunk;  //确定好了需要分配的一块

    //将分配好的内存chunk链接到相应的指针端口,并初始化next_obj
    obj* current_obj, *next_obj;
    *my_free_list = next_obj = (obj*)(chunk + size);

    //利用嵌入式指针完成分配的内存的链表化
    for(int i = 1; ; i ++) {
        current_obj = next_obj; 
        next_obj = (obj*)((char*)next_obj + size);
        if(i == nobjs - 1)  {
            current_obj->next = nullptr; 
            break; 
        }else {
            current_obj->next = next_obj; 
        }
    }

    return (result);
}


char* alloc_default::chunk_alloc(size_t size, int& nobjs) {

    size_t total_bytes = size * nobjs;
    size_t bytes_left = end_free  - start_free;
    char* result;

    if(bytes_left >= total_bytes) {   //pool空间满足申请内存

        result = start_free; 
        start_free += total_bytes;  
        return (result);
    }else if(bytes_left >= size) {  //pool空间满足一块以上
    
        nobjs = bytes_left / size;   //改变需求
        total_bytes  = size * nobjs;
        result = start_free;
        start_free +=  total_bytes;
        return (result); 
    }else {    //pool空间不满足(碎片)
            /*
             
                运行逻辑:
                按照申请的size，如果分配器的chunk都小于等于size;诉求第一级分配器即malloc
                然后确定需要挂载的chunk区块,如果区块有空闲的内存则直接分配，没有的话调用
                refill()在周边区块上补充
            */                
        size_t bytes_to_get = 2 * total_bytes + ROUND_UP(heap_size >> 4);
        if(bytes_to_get > 0) {
            obj** my_free_list = free_list + FREELIST_INDEX(bytes_left); 
             ((obj*)start_free)->next = *my_free_list; 
             *my_free_list = (obj*)start_free;
        }

        start_free = (char*)malloc(bytes_to_get);
        if(0 == start_free) {  //如果没有分配成成功
            obj** my_free_list, *p;
            for(int i = size; i <= _max; i += _align) {
                my_free_list = free_list + FREELIST_INDEX(i);    
                p = *my_free_list;
                if(0 != p) {
                    *my_free_list = p->next; 
                    start_free  = (char*)p; 
                    end_free = start_free + i;
                    return (chunk_alloc(size, nobjs));
                } 
            } 
            end_free = 0;
            start_free = (char*)malloc_alloc::allocate(bytes_to_get);
        }
        
        heap_size += bytes_to_get;
        end_free = start_free + bytes_to_get;
        return (chunk_alloc(size, nobjs));

    }
}
typedef alloc_default alloc;

int main(void)
{
    
    return 0;
}
