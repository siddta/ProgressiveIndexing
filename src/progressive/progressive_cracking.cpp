#include "../include/progressive/incremental.h"
#include "../include/util/binary_search.h"
#include "../include/full_index/hybrid_radix_insert_sort.h"
#include "../include/progressive/constants.h"


void range_query_incremental_cracking_recursive(Column& c, QuicksortNode& node, ResultStruct& results, int64_t low, int64_t high, ssize_t &remaining_swaps)
{
    int64_t *index = c.qs_index.data;
    size_t* pointers = c.qs_index.index;
    if (low <= node.min && high >= node.max) {
        // query contains entire node, just add all the entries to the result
        if (node.sorted) {
            // node is already fully done swapping
            // just add results
            for (size_t i = node.start; i < node.end; i++) {
                results.push_back(index[i]);
            }
            return;
        }
    }
    if (node.sorted) {
        range_query_sorted_subsequent_value(
                c.qs_index.data + node.start, node.end - node.start, low,
                high, node.min, node.max, results);
        return;
    }

    if (node.left < 0) {
        if (node.min == node.max) {
            node.sorted = true;
            SortedCheck(c, node.parent >= 0 ? c.qs_index.nodes[node.parent]
                                            : c.qs_index.root);
            range_query_sorted_subsequent_value(
                    c.qs_index.data + node.start, node.end - node.start, low,
                    high, node.min, node.max, results);
            return;
        }
        else if ((node.end - node.start) <= 8192) {
            // node is very small, just sort it normally
            itqs(c.qs_index.data + node.start, c.qs_index.index + node.start, node.end - node.start);
            node.sorted = true;
            SortedCheck(c, node.parent >= 0 ? c.qs_index.nodes[node.parent]
                                            : c.qs_index.root);
            remaining_swaps -= (node.end - node.start) * 25; // log2(8192)
            range_query_sorted_subsequent_value(
                    c.qs_index.data + node.start, node.end - node.start, low,
                    high, node.min, node.max, results);
            return;
        }
        // we pivot again here
        // first if we have already done some swaps
        // look into the parts we have swapped already
        if (low < node.pivot) {
            for (size_t i = node.start; i < node.current_start; i++) {
                int matching = index[i] >= low && index[i] <= high;
				results.maybe_push_back(index[i], matching);
            }
        }
        if (high >= node.pivot) {
            for (size_t i = node.current_end + 1; i < node.end; i++) {
                int matching = index[i] >= low && index[i] <= high;
				results.maybe_push_back(index[i], matching);
            }
        }

        // now we crack some pieces baby
        ssize_t elements_in_piece = node.current_end - node.current_start;
        ssize_t swaps_to_do = std::min((ssize_t) remaining_swaps, elements_in_piece);
        remaining_swaps -= swaps_to_do;
        for(size_t i = 0; i < swaps_to_do; i += 2) {
            while(index[node.current_start] < node.pivot && node.current_start < node.current_end) {
                int matching = index[node.current_start] >= low && index[node.current_start] <= high;
                results.maybe_push_back(index[node.current_start], matching);
                node.current_start++;
                i++;
            }
            while(index[node.current_end] >= node.pivot && node.current_end > node.current_start) {
                int matching = index[node.current_end] >= low && index[node.current_end] <= high;
                results.maybe_push_back(index[node.current_end], matching);
                node.current_end--;
                i++;
            }

            if (node.current_end <= node.current_start) {
                break;
            }

            // add left and right to result (potentially)
            int matching_left = index[node.current_start] >= low && index[node.current_start] <= high;
            results.maybe_push_back(index[node.current_start], matching_left);

            int matching_right = index[node.current_end] >= low && index[node.current_end] <= high;
            results.maybe_push_back(index[node.current_end], matching_right);

            // swap left and right
            std::swap(index[node.current_start], index[node.current_end]);
            std::swap(pointers[node.current_start], pointers[node.current_end]);

            node.current_start++;
            node.current_end--;
        }

        // scan the remainder (if any)
        for (size_t i = node.current_start; i <= node.current_end; i++) {
            int matching = index[i] >= low && index[i] <= high;
			results.maybe_push_back(index[i], matching);
        }
        if (node.current_start >= node.current_end) {
            if (node.current_start == node.start ||
                node.current_end == node.end - 1) {
                // either the left or right side would have zero entries
                // this means either (1) the pivot was chosen poorly
                //                or (2) the whole chunk only contains the same value
                int64_t old_pivot = node.pivot;
                // otherwise in (1) we pivot again, but move the pivot closer to the min or max
                if (node.current_start == node.start) {
                    assert(node.pivot != node.max);
                    node.pivot = node.pivot / 2 + node.max / 2;
                } else {
                    assert(node.pivot != node.min);
                    node.pivot = node.pivot / 2 + node.min / 2;
                }
                if (node.pivot == old_pivot) {
                    // everything apparently has the same value
                    node.sorted = true;
                    SortedCheck(c, node.parent >= 0 ? c.qs_index.nodes[node.parent]
                                                    : c.qs_index.root);
                    return;
                }
                node.current_start = node.start;
                node.current_end = node.end - 1;
                return;
            }

            node.left = c.qs_index.nodes.size();
            QuicksortNode left(node.left, node.position);
            left.start = node.start;
            left.end = index[node.current_start] < node.pivot
                       ? node.current_start + 1
                       : node.current_start;
            left.current_start = left.start;
            left.current_end = left.end - 1;
            left.min = node.min;
            left.max = node.pivot;
            left.pivot = (index[left.current_start] + left.max) / 2;
            node.right = c.qs_index.nodes.size() + 1;
            QuicksortNode right(node.right, node.position);
            right.start = left.end;
            right.end = node.end;
            right.current_start = right.start;
            right.current_end = right.end - 1;
            right.min = node.pivot;
            right.max = node.max;
            right.pivot = (index[right.current_start] + right.max) / 2;
            c.qs_index.nodes.push_back(left);
            c.qs_index.nodes.push_back(right);
        }
        return;
    } else {
        auto left = node.left;
        auto right = node.right;
        auto pivot = node.pivot;
        assert(left >= 0 && right >= 0);
        // node has children, go into one of the children
        if (low < pivot) {
            range_query_incremental_cracking_recursive(
				c, c.qs_index.nodes[left], results, low, high, remaining_swaps);
        }
        if (high >= pivot) {
            range_query_incremental_cracking_recursive(
				c, c.qs_index.nodes[right], results, low, high, remaining_swaps);
        }
    }
}

ResultStruct range_query_incremental_cracking(Column& c, int64_t low, int64_t high, double delta)
{
    if (!c.qs_index.index) {
        // fill the initial sortindex
        // choose an initial pivot point
        c.qs_index.index = (size_t*) malloc(sizeof(size_t) * c.data.size());
        c.qs_index.data = (int64_t*) malloc(sizeof(int64_t) * c.data.size());
        c.qs_index.root.pivot = (c.data.front() + c.data.back()) / 2;
        c.qs_index.root.start = 0;
        c.qs_index.root.end = c.data.size();
        c.qs_index.root.current_start = c.qs_index.root.start;
        c.qs_index.root.current_end = c.qs_index.root.end - 1;
        c.qs_index.root.min = INT_MIN;
        c.qs_index.root.max = INT_MAX;
        c.qs_index.current_position = 0;
    }
    ResultStruct results;
    results.reserve(c.data.size());

    if (c.qs_index.root.sorted) {
        c.converged = true;
        c.final_data = c.qs_index.data;
        // array is sorted entirely already
        range_query_sorted_subsequent_value(c.qs_index.data, c.data.size(), low, high, results);
        return results;
    }

    ssize_t remaining_swaps = (ssize_t)(c.data.size() * delta);

    // start doing swaps
    bool initial_run = c.qs_index.root.left < 0;
    if (initial_run) {
        int64_t* index = c.qs_index.data;
        size_t* pointers = c.qs_index.index;

        // for the initial run, we write the indices instead of swapping them
        // because the current array has not been initialized yet
        QuicksortNode& node = c.qs_index.root;
        // first look through the part we have already pivoted
        // for data that matches the points
        if (low < node.pivot) {
            for(size_t i = 0; i < node.current_start; i++) {
                int matching = index[i] >= low &&
                               index[i] <= high;
                results.maybe_push_back(index[i], matching);
            }
        }
        if (high >= node.pivot) {
            for(size_t i = node.current_end + 1; i < c.data.size(); i++) {
                int matching = index[i] >= low &&
                               index[i] <= high;
                results.maybe_push_back(index[i], matching);
            }
        }
        // now we start filling the index with at most remaining_swap entries
        size_t next_index = std::min(c.qs_index.current_position + remaining_swaps, c.data.size());
        remaining_swaps -= next_index - c.qs_index.current_position;
        for(size_t i = c.qs_index.current_position; i < next_index; i++) {
            int matching = c.data[i] >= low && c.data[i] <= high;
            results.maybe_push_back(c.data[i], matching);

            int bigger_pivot = c.data[i] >= node.pivot;
            int smaller_pivot = 1 - bigger_pivot;;

            index[node.current_start] = c.data[i];
            index[node.current_end] = c.data[i];
            pointers[node.current_start] = i;
            pointers[node.current_end] = i;

            node.current_start += smaller_pivot;
            node.current_end -= bigger_pivot;
        }
        c.qs_index.current_position = next_index;
        if (next_index == c.data.size()) {
            // we are finished with the initial run
            // construct the left and right side of the root node
            QuicksortNode left(0);
            left.start = node.start;
            left.end = index[node.current_start] < node.pivot ?
                       node.current_start + 1 : node.current_start;
            left.current_start = left.start;
            left.current_end = left.end - 1;
            left.pivot = (index[left.current_start] + index[left.current_end]) / 2;
            left.min = node.min;
            left.max = node.pivot;
            QuicksortNode right(1);
            right.start = left.end;
            right.end = node.end;
            right.current_start = right.start;
            right.current_end = right.end - 1;
            right.pivot = (index[right.current_start] + index[right.current_end]) / 2;
            right.min = node.pivot;
            right.max = node.max;
            c.qs_index.root.left = c.qs_index.nodes.size();
            c.qs_index.nodes.push_back(left);
            c.qs_index.root.right = c.qs_index.nodes.size();
            c.qs_index.nodes.push_back(right);
            c.qs_index.current_pivot = 0;
        } else {
            // we have done all the swapping for this run
            // now we query the remainder of the data
            for(size_t i = c.qs_index.current_position; i < c.data.size(); i++) {
                int matching = c.data[i] >= low &&
                               c.data[i] <= high;
                results.maybe_push_back(c.data[i], matching);
            }
        }
    } else {
        range_query_incremental_cracking_recursive(c, c.qs_index.root, results, low, high, remaining_swaps);
    }

    if (remaining_swaps > 0) {
        // pivot this node
        while(remaining_swaps > 0 && c.qs_index.current_pivot < c.qs_index.nodes.size()) {
            QuicksortNode& node = c.qs_index.nodes[c.qs_index.current_pivot];
            if (node.sorted || node.left >= 0) {
                c.qs_index.current_pivot++;
                continue;
            }
            if (node.min == node.max) {
                node.sorted = true;
                SortedCheck(c, node.parent >= 0 ? c.qs_index.nodes[node.parent]
                                                : c.qs_index.root);
                c.qs_index.current_pivot++;
            } else if ((node.end - node.start) <= 8192) {
                // node is very small, just sort it normally
                itqs(c.qs_index.data + node.start, c.qs_index.index + node.start, node.end - node.start);
                node.sorted = true;
                SortedCheck(c, node.parent >= 0 ? c.qs_index.nodes[node.parent]
                                                : c.qs_index.root);
                remaining_swaps -= (node.end - node.start) * 20; // log2(8192)
                c.qs_index.current_pivot++;
            } else {
                int64_t *index = c.qs_index.data;
                size_t* pointers = c.qs_index.index;

                ssize_t elements_in_piece = node.current_end - node.current_start;
                ssize_t swaps_to_do = std::min((ssize_t) remaining_swaps, elements_in_piece);
                remaining_swaps -= swaps_to_do;
                for(size_t i = 0; i < swaps_to_do; i += 2) {
                    while(index[node.current_start] < node.pivot && node.current_start < node.current_end) {
                        int matching = index[node.current_start] >= low && index[node.current_start] <= high;
                        node.current_start++;
                        i++;
                    }
                    while(index[node.current_end] >= node.pivot && node.current_end > node.current_start) {
                        int matching = index[node.current_end] >= low && index[node.current_end] <= high;
                        node.current_end--;
                        i++;
                    }

                    if (node.current_end <= node.current_start) {
                        break;
                    }

                    // swap left and right
                    std::swap(index[node.current_start], index[node.current_end]);
                    std::swap(pointers[node.current_start], pointers[node.current_end]);

                    node.current_start++;
                    node.current_end--;
                }
                // // now we start swapping stuff
                // while (node.current_start < node.current_end &&
                //        remaining_swaps > 0) {
                //     int64_t start = index[node.current_start];
                //     int64_t end = index[node.current_end];
                //     size_t start_pointer = pointers[node.current_start];
                //     size_t end_pointer = pointers[node.current_end];

                //     int start_has_to_swap = start >= node.pivot;
                //     int end_has_to_swap = end < node.pivot;
                //     int has_to_swap = start_has_to_swap * end_has_to_swap;

                //     index[node.current_start] =
                //         !has_to_swap * start + has_to_swap * end;
                //     index[node.current_end] =
                //         !has_to_swap * end + has_to_swap * start;
                //     pointers[node.current_start]
                //         = !has_to_swap * start_pointer +
                //            has_to_swap * end_pointer;
                //     pointers[node.current_end]
                //         = !has_to_swap * end_pointer +
                //            has_to_swap * start_pointer;

                //     node.current_start += !start_has_to_swap + has_to_swap;
                //     node.current_end -= !end_has_to_swap + has_to_swap;
                //     remaining_swaps--;
                // }
                if (node.current_start >= node.current_end) {
                    if (node.current_start == node.start ||
                        node.current_end == node.end - 1) {
                        // either the left or right side would have zero entries
                        // this means either (1) the pivot was chosen poorly
                        //                or (2) the whole chunk only contains the same value
                        // first check for (2)
                        // otherwise in (1) we pivot again, but move the pivot closer to the min or max
                        if (node.current_start == node.start) {
                            assert(node.pivot != node.max);
                            node.pivot = node.pivot / 2 + node.max / 2;
                        } else {
                            assert(node.pivot != node.min);
                            node.pivot = node.pivot / 2 + node.min / 2;
                        }
                        node.current_start = node.start;
                        node.current_end = node.end - 1;
                        break;
                    }
                    node.left = c.qs_index.nodes.size();
                    QuicksortNode left(node.left, node.position);
                    left.start = node.start;
                    left.end = index[node.current_start] < node.pivot
                               ? node.current_start + 1
                               : node.current_start;
                    left.current_start = left.start;
                    left.current_end = left.end - 1;
                    left.min = node.min;
                    left.max = node.pivot;
                    left.pivot =
                            (index[left.current_start] + index[left.current_end]) / 2;
                    node.right = c.qs_index.nodes.size() + 1;
                    QuicksortNode right(node.right, node.position);
                    right.start = left.end;
                    right.end = node.end;
                    right.current_start = right.start;
                    right.current_end = right.end - 1;
                    right.pivot =
                            (index[right.current_start] + index[right.current_end]) / 2;
                    right.min = node.pivot;
                    right.max = node.max;
                    c.qs_index.nodes.push_back(left);
                    c.qs_index.nodes.push_back(right);
                    c.qs_index.current_pivot++;
                }
            }
        }
    }
    return results;
}




QuicksortNode& FindLowestNode(Column &c, QuicksortNode& node, int64_t value) {
    if (node.left < 0) {
        return node;
    }
    return FindLowestNode(c, c.qs_index.nodes[value < node.pivot ? node.left : node.right], value);
}

size_t GetHeight(Column &c, QuicksortNode& node) {
    if (node.left < 0) {
        return 1;
    }
    return 1 + std::max(GetHeight(c, c.qs_index.nodes[node.left]), GetHeight(c, c.qs_index.nodes[node.right]));
}