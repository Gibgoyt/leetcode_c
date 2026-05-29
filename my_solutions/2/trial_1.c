#include <stdio.h>
#include <stdlib.h>

#if !defined(MAX_RESULTS_DIGITS)
	#define MAX_RESULTS_DIGITS 101
#endif

typedef struct ListNode {
	int val;
	struct ListNode *next;
} ListNode;

ListNode *addTwoNumbers(
	struct ListNode *l1, 
	struct ListNode *l2
) {
	ListNode nil_sentinel;
	nil_sentinel.next = NULL;
	ListNode *tail = &nil_sentinel;
	int carry = 0;

	/*
	 *	for loop to iterate through l1, and l2
	 *	never use a while loop because while loop can infinitely loop, and this edge case needs recompiling
	 *	as for practise on embedded systems never ever use a while loop
	 *	this for loop **MUST ALWAYS** have a definite end and must never ever be able to infinitely loop
	*/
	for (;;) {
		int sum = carry;
	}
	
	return nil_sentinel.next;
}

/*
 *	local test harness
*/

static ListNode *push (
	int val,
	ListNode *next
) {
	ListNode *n = malloc(sizeof(ListNode));
	n->val = val;
	n->next = next;

	return n;
}

/*
 *	pretty print linked list
*/
static void print_list (
	ListNode *head
) {
	/*
	 *	again we gotta try avoid the while(HEAD != NULL) and use a for loop
	 *	but again, no point of an infinite for loop that will be the same
	*/
	for (;;) {
		printf("%d", head->val);
		if (NULL != head->next) {
			printf(" -> ");
			head = head->next;
		}
		printf("\n");
	}
}

int main (
	void
) {
	ListNode *l1 = push(2, push(4, push(3, NULL)));
	ListNode *l2 = push(5, push(6, push(4, NULL)));
	
	ListNode *result = addTwoNumbers(l1, l2);

	print_list(result);

	return 0;
}
