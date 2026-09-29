import { useState } from 'react';
import ArchitectureTab from './components/ArchitectureTab';
import FilesTab from './components/FilesTab';
import IntegrationTab from './components/IntegrationTab';
import OpcodeTab from './components/OpcodeTab';
import RegisterMapTab from './components/RegisterMapTab';
import BuildTab from './components/BuildTab';

const TABS = [
  { id: 'arch',    label: '🏗 Architecture',     short: 'Arch'     },
  { id: 'files',   label: '📁 Source Files',      short: 'Files'    },
  { id: 'opcodes', label: '📋 Opcode Coverage',   short: 'Opcodes'  },
  { id: 'regmap',  label: '🗂 Register Map',       short: 'RegMap'   },
  { id: 'integ',   label: '🔌 Integration Guide',  short: 'Integrate'},
  { id: 'build',   label: '🔨 Build & Makefile',   short: 'Build'    },
];

export default function App() {
  const [activeTab, setActiveTab] = useState('arch');

  return (
    <div className="min-h-screen bg-gray-950 text-gray-100 font-mono">
      {/* Header */}
      <header className="bg-gray-900 border-b border-green-700 shadow-lg">
        <div className="max-w-screen-2xl mx-auto px-4 py-4">
          <div className="flex flex-col sm:flex-row sm:items-center gap-2">
            <div className="flex items-center gap-3">
              <span className="text-3xl">🎮</span>
              <div>
                <h1 className="text-xl md:text-2xl font-bold text-green-400 leading-tight">
                  NooDS-Wii ARMv4 → PPC JIT Recompiler
                </h1>
                <p className="text-xs text-gray-400 mt-0.5">
                  GBA/NDS ARM7TDMI (THUMB+ARM) → Broadway PowerPC 750 — Full Implementation Reference
                </p>
              </div>
            </div>
            <div className="sm:ml-auto flex gap-2 flex-wrap">
              <a href="https://github.com/radicalten/NooDS-Wii" target="_blank" rel="noreferrer"
                 className="text-xs bg-gray-800 hover:bg-gray-700 border border-gray-600 px-3 py-1 rounded text-green-300 transition-colors">
                NooDS-Wii ↗
              </a>
              <a href="https://github.com/dborth/vbagx/tree/master/source/vba/gba" target="_blank" rel="noreferrer"
                 className="text-xs bg-gray-800 hover:bg-gray-700 border border-gray-600 px-3 py-1 rounded text-blue-300 transition-colors">
                VBA-GX JIT Ref ↗
              </a>
            </div>
          </div>
        </div>
      </header>

      {/* Tab Bar */}
      <nav className="bg-gray-900 border-b border-gray-700 sticky top-0 z-20">
        <div className="max-w-screen-2xl mx-auto px-2 overflow-x-auto">
          <div className="flex">
            {TABS.map(tab => (
              <button
                key={tab.id}
                onClick={() => setActiveTab(tab.id)}
                className={`px-3 md:px-5 py-3 text-xs md:text-sm font-medium whitespace-nowrap border-b-2 transition-colors ${
                  activeTab === tab.id
                    ? 'border-green-400 text-green-300 bg-gray-800'
                    : 'border-transparent text-gray-400 hover:text-gray-200 hover:border-gray-500'
                }`}
              >
                <span className="hidden md:inline">{tab.label}</span>
                <span className="md:hidden">{tab.short}</span>
              </button>
            ))}
          </div>
        </div>
      </nav>

      {/* Content */}
      <main className="max-w-screen-2xl mx-auto px-2 md:px-4 py-6">
        {activeTab === 'arch'    && <ArchitectureTab />}
        {activeTab === 'files'   && <FilesTab />}
        {activeTab === 'opcodes' && <OpcodeTab />}
        {activeTab === 'regmap'  && <RegisterMapTab />}
        {activeTab === 'integ'   && <IntegrationTab />}
        {activeTab === 'build'   && <BuildTab />}
      </main>

      <footer className="border-t border-gray-800 mt-10 py-4 text-center text-xs text-gray-600">
        NooDS-Wii JIT Reference — ARMv4 (GBA/NDS ARM7) → PowerPC 750 (Broadway/Gekko) — Based on dborth/vbagx JIT architecture
      </footer>
    </div>
  );
}
