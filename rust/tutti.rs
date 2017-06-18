use std::collections::VecDeque;
use std::mem;

type Address             = u16;
type Register            = Address;
type InstructionWordSize = Address;

#[derive(Debug)]
struct Cpu {
    ax: Register,
    bx: Register,
    cx: Register,
    dx: Register,
    sp: Register,
    ip: Register,
    flags: u8,
    stack: [Register; 10],
}

impl Cpu {
    fn new() -> Cpu
    {
        Cpu {
            ax: 0,
            bx: 0,
            cx: 0,
            dx: 0,
            sp: 0,
            ip: 0,
            flags: 0,
            stack: [0; 10],
        }
    }

    fn push(&mut self, v: Register)
    {
        self.stack[self.sp as usize] = v;
        self.sp += 1;
    }

    fn pop(&mut self) -> Register
    {
        let v = self.stack[self.sp as usize];
        self.sp -= 1;
        v
    }
}

#[derive(Copy, Clone, PartialEq, Eq)]
enum Instruction {
    Nop0   = 0x00, // no operation
    Nop1   = 0x01, // no operation
    Or1    = 0x02, // flip low order bit of cx, cx ^= 1
    Shl    = 0x03, // shift left cx register, cx <<= 1
    Zero   = 0x04, // set cx register to zero, cx = 0
    IfCz   = 0x05, // if cx == 0 execute next instruction
    SubAb  = 0x06, // subtract bx from ax, cx = ax - bx
    SubAc  = 0x07, // subtract cx from ax, ax = ax - cx
    IncA   = 0x08, // increment ax, ax = ax + 1
    IncB   = 0x09, // increment bx, bx = bx + 1
    DecC   = 0x0A, // decrement cx, cx = cx - 1
    IncC   = 0x0B, // increment cx, cx = cx + 1
    PushAx = 0x0C, // push ax on stack
    PushBx = 0x0D, // push bx on stack
    PushCx = 0x0E, // push cx on stack
    PushDx = 0x0F, // push dx on stack
    PopAx  = 0x10, // pop top of stack into ax
    PopBx  = 0x11, // pop top of stack into bx
    PopCx  = 0x12, // pop top of stack into cx
    PopDx  = 0x13, // pop top of stack into dx
    Jmp    = 0x14, // move ip to template
    Jmpb   = 0x15, // move ip backward to template
    Call   = 0x16, // call a procedure
    Ret    = 0x17, // return from a procedure
    MovCd  = 0x18, // move cx to dx, dx = cx
    MovAb  = 0x19, // move ax to bx, bx = ax
    MovIab = 0x1A, // move instruction at address in bx to address in ax
    Adr    = 0x1B, // address of nearest template to ax
    Adrb   = 0x1C, // search backward for template
    Adrf   = 0x1D, // search forward for template
    Mal    = 0x1E, // allocate memory for daughter cell
    Divide = 0x1F, // cell division
}

struct Creature {
    core: Cpu,
    genome_index: usize,
    genome_actual_size: usize,
}

impl Creature {
    fn new(i: usize) -> Creature {
        Creature {
            core: Cpu::new(),
            genome_index: i,
            genome_actual_size: 0,
        }
    }
}

// type MemoryWriteListener = Fn(GenomeRegion, &[Instruction])

const UNIVERSE_TOTAL_GENOME_CAPACITY: usize = 6_0000;
const GENOME_BLOCK_UNIT_SIZE: usize         = 500;
const BITMAP_SIZE: usize                    = UNIVERSE_TOTAL_GENOME_CAPACITY / GENOME_BLOCK_UNIT_SIZE;

struct Universe {
    genome_pool: [Instruction; UNIVERSE_TOTAL_GENOME_CAPACITY],
    bitmap: [bool; BITMAP_SIZE],
    creatures: VecDeque<Creature>,
}

impl Universe {
    fn new() -> Universe
    {
        Universe {
            genome_pool: [Instruction::Nop0; UNIVERSE_TOTAL_GENOME_CAPACITY],
            bitmap: [false; BITMAP_SIZE],
            creatures: VecDeque::new(),
        }
    }

    fn born_creature(&mut self, instrunctions: &[Instruction])
    {
        match self.allocate_genome_pool() {
            None => panic!("no memory"),
            Some(i) => {
                let mut c = Creature::new(i);
                c.genome_actual_size = instrunctions.len();
                self.creatures.push_front(c);
                self.write_to_genome_pool(i, 0, instrunctions);
            }
        }
    }

    fn allocate_genome_pool(&mut self) -> Option<usize>
    {
        for i in 0..self.bitmap.len() {
            if self.bitmap[i] == false {
                self.bitmap[i] == true;
                return Some(i);
            }
        }

        None
    }

    fn free_genome_pool(&mut self, i: usize)
    {
        self.bitmap[i] = false;
    }

    fn read_from_genome_pool(&self, i: usize, addr: usize, size: usize) -> &[Instruction]
    {
        let begin = i * GENOME_BLOCK_UNIT_SIZE + addr;
        let end = begin + size;
        &self.genome_pool[begin..end]
    }

    fn write_to_genome_pool(&mut self, i: usize, addr: usize, data: &[Instruction])
    {
        let mut k = i * GENOME_BLOCK_UNIT_SIZE + addr;
        for j in 0..data.len() {
            self.genome_pool[k] = data[j];
            k = k + 1;
        }
    }

    fn search_templete(&self, template: &[Instruction], begin_addr: usize)
    {
        let complement_template: Vec<Instruction> = template.clone().into_iter().map(|&x| {
            use Instruction::*;
            match x {
                Nop0 => Nop1,
                Nop1 => Nop0,
                _ => panic!("invalid instrunction"),
            }
        }).collect();

        const SEARCH_LIMIT: usize = 1000;
        // fn find_subsequence(haystack: &[u8], needle: &[u8]) -> Option<usize> {
        //     haystack.windows(needle.len()).position(|window| window == needle)
        // }

        let end_addr = begin_addr + SEARCH_LIMIT;
        let search_region = &self.genome_pool[begin_addr..(end_addr)];

        let t = search_region.windows(complement_template.len()).position(|window| {
            let cnt = 0;
            for i in window.iter() {
                if complement_template[cnt] != *i {
                    return false;
                }
            }
            true
        });
        println!("{:?}", t);
    }

    fn execute(&self, cpu: &mut Cpu, ins: Instruction)
    {
        use Instruction::*;
        let (ax, bx, cx, dx) = (cpu.ax, cpu.bx, cpu.cx, cpu.dx);
        match ins {
            Nop0   => {},
            Nop1   => {},
            Or1    => cpu.cx = cx ^ 1,
            Shl    => cpu.cx = cx << 1,
            Zero   => cpu.cx = 0,
            IfCz   => {
                if cx != 0 {
                    // Skip the next instruction.
                    cpu.ip += 1;
                }
            },
            SubAb  => cpu.cx = ax - bx,
            SubAc  => cpu.ax = ax - cx,
            IncA   => cpu.ax = ax + 1,
            IncB   => cpu.bx = bx + 1,
            DecC   => cpu.cx = cx - 1,
            IncC   => cpu.cx = cx + 1,
            PushAx => cpu.push(ax),
            PushBx => cpu.push(bx),
            PushCx => cpu.push(cx),
            PushDx => cpu.push(dx),
            PopAx  => cpu.ax = cpu.pop(),
            PopBx  => cpu.bx = cpu.pop(),
            PopCx  => cpu.cx = cpu.pop(),
            PopDx  => cpu.dx = cpu.pop(),
            Jmp => {
                //TODO
            },
            Jmpb => {
                //TODO
            },
            Call => {
            },
            MovCd => {
                cpu.dx = cpu.cx
            },
            MovAb => {
                cpu.bx = cpu.ax
            },
            MovIab => {
                // TODO
            },
            Adr => {
                // TODO
            },
            Adrb => {
                // TODO
            },
            Adrf => {
                // TODO
            },
            Mal => {
                // TODO
            },
            Divide => {
                // TODO
            },
            _  => panic!("No implementation")
        }
    }

    fn give_cpu_time(&self, creature: &mut Creature)
    {
        const SLICED_TIME: usize = 10;

        let cpu = &mut creature.core;
        for _ in 0..SLICED_TIME {
            // fetch
            let ins = self.read_from_genome_pool(creature.genome_index, cpu.ip as usize, 1)[0];
            self.execute(cpu, ins);
            cpu.ip += 1;

            if (cpu.ip as usize) == creature.genome_actual_size {
                cpu.ip = 0;
            }

            println!("CPU: {:?}", cpu);
        }
    }

    fn works(&mut self)
    {
        let mut cs = mem::replace(&mut self.creatures, VecDeque::new());
        for i in cs.iter_mut() {
            self.give_cpu_time(i);
        }
        self.creatures = cs;
    }
}

fn main() {
    use Instruction::*;

    let mut univ = Universe::new();
    let insts = [
        Nop1,
        Nop1,
        Nop1,
        Nop1,
        Zero,
        Or1,
        Shl,
        Shl,
    ];
    univ.born_creature(&insts);
    univ.works();
}

// void time_slice(int  ci)
// {   Pcells  ce; /* pointer to the array of cell structures */
//     char    i;  /* instruction from soup */
//     int     di; /* decoded instruction */
//     int     j, size_slice;
//     ce = cells + ci;
//     for(j = 0; j < size_slice; j++)
//     {   i = fetch(ce->c.ip); /* fetch instruction from soup, at address ip */
//         di = decode(i);      /* decode the fetched instruction */
//         execute(di, ci);     /* execute the decoded instruction */
//         increment_ip(di,ce); /* move instruction pointer to next instruction */
//         system_work(); /* opportunity to extract information */
//     }
// }
