# Order Matching Engine (OME)

A high-performance Order Matching Engine implemented in **modern C++**, featuring:

- Limit & Market Orders  
- Price–Time Priority Matching  
- Multiple Order Types (GTC, FOK, FAK, GFD, Market)  
- FIFO Queueing at each price level  
- Clean OOP design (Order, OrderBook, MatchingEngine)  
- Unit tests using **GoogleTest**  
- Build system based on **MinGW + Makefile**  

This project is designed as an HFT-style interview project to demonstrate clean architecture, performance-aware coding, and testability.

---

## 🌱 Folder Structure

```
include/        # Header files
src/            # Engine implementation
tests/          # GoogleTest unit tests
googletest/     # Embedded GoogleTest library
Makefile        # Build system
```
---
## 🛠 Build Instructions (Windows - MinGW)
```bash
mingw32-make clean
mingw32-make
mingw32-make test
```
---
⚙️ Requirements
- MinGW-w64
- C++17-compatible compiler (g++)
- mingw32-make
- No external dependencies — GoogleTest is included in this repository.

