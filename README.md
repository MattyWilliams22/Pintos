# Pintos Operating System Framework

This project is an extension of the Pintos simple operating system framework. It was a group project developed during the autumn term of our second year at university. The goal was to enhance Pintos with several advanced features and functionalities.

## Features

- **Thread Sleeping**: Implemented using semaphores.
- **Thread Scheduling**:
  - Priority scheduling.
  - BSD scheduling.
- **User Programs**:
  - Loading and execution of user programs.
- **Virtual Memory**: Added support for virtual memory to enable filesystem interaction.

## Repository Structure

- **`src/`**: Contains the source code for the Pintos operating system, divided into various subdirectories for threads, user programs, virtual memory, and more.
- **`design_docs/`**: Contains detailed design documents for the implemented features.
- **`tests/`**: Includes test cases to verify the functionality of the implemented features.
- **`doc/`**: Additional project-related documentation.

## Technologies Used

- **C**: Core language used for development.
- **x86 Assembly**: Low-level system programming.
- **Makefile**: For build automation.

## Getting Started

### Prerequisites

- GCC Compiler.
- QEMU or Bochs (x86 emulator).
- GNU Make.

### Installation

1. Clone the repository:
   ```bash
   git clone https://github.com/MattyWilliams22/Pintos.git
   cd Pintos
   ```

2. Navigate to the src/ directory and build the project:
   ```bash
   cd src
   make
   ```

### License

This project is based on the Pintos framework, which is licensed by Stanford University. Refer to the LICENSE file for more details.
