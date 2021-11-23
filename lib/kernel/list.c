#include "list.h"
#include "interrupt.h"
#include "global.h"

void list_init(struct list* list) {
	list->head.next = &list->tail;
	list->tail.prev = &list->head;
}

int list_empty(struct list* list){
	
	return list->head.next == &list->tail ? 1 : 0;
}

//将elem插入到before的前面
void list_insert(struct list_elem* before, struct list_elem* elem){
	
	enum intr_status old_status = intr_disable();
	before->prev->next = elem;
	elem->prev = before->prev;
	elem->next = before;
	before->prev = elem;
	
	intr_set_status(old_status);
}

//将elem添加到链表首部
void list_push(struct list* list, struct list_elem* elem) {
	list_insert(list->head.next,elem);
}

//将elem添加到链表尾部
void list_append(struct list* list, struct list_elem* elem) {
	list_insert(&list->tail,elem);
}

void list_remove(struct list_elem* elem){
	enum intr_status old_status = intr_disable();
	
	elem->prev->next = elem->next;
	elem->next->prev = elem->prev;
	intr_set_status(old_status);
}

//将链表第一个元素删除并返回
struct list_elem* list_pop(struct list* list){
	
	struct list_elem* elem = list->head.next;
	list_remove(elem);
	return elem;
}

int elem_find(struct list* list, struct list_elem* elem) {
	
	struct list_elem* temp = list->head.next;
	while(temp != &list->tail) {
		if(temp == elem) {
			return 1;
		}
		temp = temp->next;
	}
	return 0;
	
}

struct list_elem* list_traversal(struct list* list, function func, int arg) {
	struct list_elem* temp = list->head.next;
	if(list_empty(list)) {
		return NULL;
	}
	
	while(temp != &list->tail) {
		if(func(temp,arg)) {
			return temp;
		}
		temp = temp->next;
	}
	return NULL;
}

uint32_t list_len(struct list* list) {
	uint32_t len = 0;
	struct list_elem* elem = list->head.next;
	while(elem != &list->tail) {
		len ++;
		elem = elem->next;
	}
	return len;
}


