/*
 * Copyright (C) 2017 CAMELab
 *
 * This file is part of SimpleSSD.
 *
 * SimpleSSD is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * SimpleSSD is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with SimpleSSD.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "sim/cpu.hh"

namespace SimpleSSD {

// Defined in sim/cpu.hh
DMAFunction cpuHandler = commonCPUHandler;

CPUContext::_CPUContext(DMAFunction &f, void *c, CPU::CPU *pCPU) : func(f), context(c), pCPU(pCPU) {}

CPUContext::_CPUContext(DMAFunction &f, void *c, CPU::NAMESPACE n,
                        CPU::FUNCTION fc, CPU::CPU *pCPU)
    : func(f), context(c), ns(n), fct(fc), delay(0), pCPU(pCPU) {}

CPUContext::_CPUContext(DMAFunction &f, void *c, CPU::NAMESPACE n,
                        CPU::FUNCTION fc, uint64_t d, CPU::CPU *pCPU)
    : func(f), context(c), ns(n), fct(fc), delay(d), pCPU(pCPU) {}

void commonCPUHandler(uint64_t, void *context) {
  CPUContext *pContext = (CPUContext *)context;
  
  pContext->pCPU->execute(pContext->ns, pContext->fct, pContext->func, pContext->context,
          pContext->delay);

  delete pContext;
}

}  // namespace SimpleSSD
