# Contains Duplicate

**Difficulty:** Easy

Given an integer array `nums`, return `true` if any value appears more than once in the array, otherwise return `false`.

## Example 1

```
Input:  nums = [1, 2, 3, 3]
Output: true
```

## Example 2

```
Input:  nums = [1, 2, 3, 4]
Output: false
```

## Constraints

- `0 <= nums.length <= 10^5`
- `-10^9 <= nums[i] <= 10^9`

## Recommended Time & Space Complexity

You should aim for a solution with **O(n)** time and **O(n)** space, where `n` is the size of the input array.

## Hints

1. A brute force solution would be to check every element against every other element in the array. This would be an O(n^2) solution. Can you think of a better way?
2. Is there a way to check if an element is a duplicate without comparing it to every other element? Maybe there's a data structure that is useful here.
