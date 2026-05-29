#include <stdio.h>
#include <stdlib.h>

#if !defined(MAX_RESULTS_DIGITS)
	#define MAX_RESULTS_DIGITS 101
#endif

typedef struct ListNode {
	int val;
	struct ListNode *next;
} ListNode;

/*
 *	function declarations
*/
static ListNode *push (
	int val,
	ListNode *next
);
static void print_list (
	ListNode *head
);
ListNode *addTwoNumbers(
	struct ListNode *l1, 
	struct ListNode *l2
);

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
	 *
	 *	every pass over l1 and/or l2 toward NULL, once both l1, l2 are NULL sum is just the carry
	 *	carry clears and the next test fails, so the loop is bounded by (max(len(l1), len(l2)) + 1)
	*/
	for (;
		NULL != l1 ||
		NULL != l2 || 
		0 != carry
	;) {
		int sum = carry;

		if (NULL != l1) {
			sum += l1->val;
			l1 = l1->next;
		}

		if (NULL != l2) {
			sum += l2->val;
			l2 = l2->next;
		}

		carry = sum / 10;

		// TODO!!: fix Incompatible integer to pointer conversion assigning to 'struct ListNode *'
		tail->next = push(sum % 10, NULL);
		tail = tail->next;
	}
	
	return nil_sentinel.next;
}

/*
 *	local test harness
 * 	TODO!!: please fix Static declaration of 'push' follows non-static declaration
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
	for (; 
		NULL != head; 
		head = head->next
	) {
		printf("%d", head->val);
		if (NULL != head->next) {
			printf(" -> ");
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
