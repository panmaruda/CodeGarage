#![feature(offset_to)]
#![cfg_attr(test, feature(allocator_api))]

extern crate list5;

use list5::LinkedList;
use list5::Node;

use std::default::Default;
use std::mem;
use std::ptr;


// 2^MAX_ORDER
const MAX_ORDER: usize = 16 + 1;


struct Frame {
    order: u8,
    is_free: bool
}


struct BuddyManager {
    nodes: *mut Node<Frame>,
    count_frames: usize,
    base_addr: usize,
    count_free_frames: [usize; MAX_ORDER],
    lists: [LinkedList<Frame>; MAX_ORDER],
}


impl Default for Frame {
    fn default() -> Frame
    {
        Frame {
            order: 0,
            is_free: true,
        }
    }
}


impl BuddyManager {
    fn new(nodes: *mut Node<Frame>, count: usize, base_addr: usize) -> BuddyManager
    {
        let lists = unsafe {
            let mut lists: [LinkedList<Frame>; MAX_ORDER] = mem::uninitialized();

            for l in lists.iter_mut() {
                ptr::write(l, LinkedList::new())
            }

            lists
        };

        let mut bman = BuddyManager {
            nodes: nodes,
            count_frames: count,
            base_addr: base_addr,
            lists: lists,
            count_free_frames: [0; MAX_ORDER],
        };

        bman.supply_frame_nodes(nodes, count);

        bman
    }

    fn push_node_frame(&mut self, order: usize, node_ptr: *mut Node<Frame>)
    {
        self.lists[order].push_back(node_ptr);
        self.count_free_frames[order] += 1;
    }

    fn pop_node_frame(&mut self, order: usize) -> Option<*mut Node<Frame>>
    {
        self.count_free_frames[order] -= 1;
        self.lists[order].pop_front()
    }

    fn is_empty_list(&self, order: usize) -> bool
    {
        self.count_free_frames[order] == 0
    }

    fn supply_frame_nodes(&mut self, nodes: *mut Node<Frame>, count: usize)
    {
        debug_assert!(count != 0);
        debug_assert!(nodes.is_null() == false);

        let mut count_rest_frames = count;
        let mut current_node_ptr  = nodes;

        for order in (0..MAX_ORDER).rev() {
            let count_frames_in_list = 1usize << order;
            while count_frames_in_list <= count_rest_frames {
                self.push_node_frame(order, current_node_ptr);

                current_node_ptr = unsafe { current_node_ptr.offset(count_frames_in_list as isize) };
                count_rest_frames -= count_frames_in_list;
            }
        }
    }

    fn get_frame_index(&self, node: *mut Node<Frame>) -> usize
    {
        match self.nodes.offset_to(node) {
            Some(offset) => offset as usize,
            None         => panic!("unknown node pointer is given."),
        }
    }

    fn get_buddy_frame(&mut self, node_ptr: *mut Node<Frame>, order: usize) -> *mut Node<Frame>
    {
        let buddy_index = self.get_frame_index(node_ptr) ^ (1 << order);
        unsafe { self.nodes.offset(buddy_index as isize) }
    }

    fn allocate_frames_by_order(&mut self, request_order: usize) -> Option<*mut Node<Frame>>
    {
        if MAX_ORDER <= request_order {
            return None;
        }

        for order in request_order..MAX_ORDER {
            if self.is_empty_list(order) {
                continue;
            }

            let node_ptr_opt = self.pop_node_frame(order);

            match node_ptr_opt {
                None           => panic!("the counter may be an error"),
                Some(node_ptr) => {
                    // Set the order and the extra parts are stored into the other lists.
                    let allocated_frame     = unsafe {node_ptr.as_mut()}.unwrap().as_mut();
                    allocated_frame.order   = request_order as u8;
                    allocated_frame.is_free = false;

                    for i in (order - 1)..(request_order - 1) {
                        let buddy_node    = self.get_buddy_frame(node_ptr, i);
                        let buddy_frame   = unsafe {buddy_node.as_mut().unwrap()}.as_mut();
                        buddy_frame.order = i as u8;

                        self.push_node_frame(i, buddy_node);
                    }
                }
            }

            return node_ptr_opt;
        }

        None
    }
}


#[cfg(test)]
mod tests {
    use super::*;

    use std::heap::{Alloc, System, Layout};
    use std::mem;
    use std::slice;

    fn allocate_node_objs<'a, T>(count: usize) -> &'a mut [T] where T: Default
    {
        let type_size = mem::size_of::<T>();
        let align     = mem::align_of::<T>();
        let layout    = Layout::from_size_align(count * type_size, align).unwrap();
        let ptr       = unsafe { System.alloc(layout) }.unwrap();
        unsafe { slice::from_raw_parts_mut(ptr as *mut T, count) }
    }

    #[test]
    fn new_buddy_manager()
    {
        let mut expected_counts = [0usize; MAX_ORDER];

        let count = 1024;
        let nodes = allocate_node_objs::<Node<Frame>>(count);

        let bman = BuddyManager::new(&mut nodes[0] as *mut _, count, 0);
        expected_counts[10] += 1;
        assert_eq!(bman.count_free_frames, expected_counts);
    }

    #[test]
    fn test_get_frame_index()
    {
        let count = 1024;
        let nodes = allocate_node_objs::<Node<Frame>>(count);
        let bman  = BuddyManager::new(&mut nodes[0] as *mut _, count, 0);

        assert_eq!(bman.get_frame_index(&mut nodes[0] as *mut _), 0);
        assert_eq!(bman.get_frame_index(&mut nodes[10] as *mut _), 10);
        assert_eq!(bman.get_frame_index(&mut nodes[1023] as *mut _), 1023);
    }

    #[test]
    fn test_get_buddy_frame()
    {
        let count = 1024;
        let nodes = allocate_node_objs::<Node<Frame>>(count);
        let bman  = BuddyManager::new(&mut nodes[0] as *mut _, count, 0);
    }

    #[test]
    fn test_allocate_frames_by_order()
    {
        let count    = 1024;
        let nodes    = allocate_node_objs::<Node<Frame>>(count);
        let mut bman = BuddyManager::new(&mut nodes[0] as *mut _, count, 0);

        let node = bman.allocate_frames_by_order(1).unwrap();
        assert_eq!(unsafe {node.as_ref()}.unwrap().as_ref().order, 1);
        assert_eq!(unsafe {node.as_ref()}.unwrap().as_ref().is_free, false);
    }
}
