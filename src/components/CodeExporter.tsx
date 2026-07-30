import React, { useState } from 'react';
import { CPP_FILES, generateFullTermuxScript } from '../data/cppFiles';
import { Terminal, Copy, Check, Download, FileCode, Package, Cpu } from 'lucide-react';

export const CodeExporter: React.FC = () => {
  const [selectedFilename, setSelectedFilename] = useState<string>('IvannaFusionCore.hpp');
  const [copiedFilename, setCopiedFilename] = useState<string | null>(null);
  const [copiedFullScript, setCopiedFullScript] = useState<boolean>(false);

  const selectedFile = CPP_FILES.find((f) => f.filename === selectedFilename) || CPP_FILES[0];
  const fullTermuxScript = generateFullTermuxScript();

  const copyToClipboard = (text: string, label: string) => {
    navigator.clipboard.writeText(text);
    if (label === 'full') {
      setCopiedFullScript(true);
      setTimeout(() => setCopiedFullScript(false), 2500);
    } else {
      setCopiedFilename(label);
      setTimeout(() => setCopiedFilename(null), 2500);
    }
  };

  const downloadFile = (filename: string, content: string) => {
    const blob = new Blob([content], { type: 'text/plain;charset=utf-8' });
    const url = URL.createObjectURL(blob);
    const link = document.createElement('a');
    link.href = url;
    link.download = filename;
    document.body.appendChild(link);
    link.click();
    document.body.removeChild(link);
    URL.revokeObjectURL(url);
  };

  return (
    <div className="space-y-6">
      
      {/* 1-Click Termux Installer Banner */}
      <div className="bg-gradient-to-r from-slate-900 via-amber-950/40 to-slate-900 border border-amber-500/40 rounded-2xl p-6 shadow-xl space-y-4">
        <div className="flex flex-col lg:flex-row lg:items-center justify-between gap-4">
          <div>
            <div className="flex items-center space-x-2">
              <Terminal className="w-5 h-5 text-amber-400" />
              <h2 className="text-lg font-bold text-slate-100 font-mono">
                Termux 1-Click Quick Deploy Command Block
              </h2>
            </div>
            <p className="text-xs text-slate-300 font-mono mt-1 max-w-2xl">
              Copies all complete C++ source files, CMake, and build scripts into a single bash command block using <code className="text-amber-300">cat &lt;&lt; &apos;EOF&apos;</code> format. Paste directly into Android Termux or Linux shell.
            </p>
          </div>

          <div className="flex items-center space-x-3">
            <button
              onClick={() => copyToClipboard(fullTermuxScript, 'full')}
              className="flex items-center space-x-2 px-5 py-2.5 rounded-xl font-mono text-xs font-bold bg-amber-500 text-slate-950 hover:bg-amber-400 transition-all shadow-lg shadow-amber-950/50"
            >
              {copiedFullScript ? <Check className="w-4 h-4" /> : <Copy className="w-4 h-4" />}
              <span>{copiedFullScript ? 'COPIED TO CLIPBOARD!' : 'COPY TERMUX BLOCK'}</span>
            </button>
          </div>
        </div>

        {/* Command Preview Box */}
        <div className="relative bg-slate-950/90 border border-slate-800 rounded-xl p-4 font-mono text-xs text-amber-200/90 overflow-x-auto max-h-28 scrollbar-thin">
          <pre>{fullTermuxScript.slice(0, 320)}...\n# [... All C++ headers, sources, CMakeLists.txt & build_and_release.sh included ...]</pre>
        </div>
      </div>

      {/* Code Browser & Tabbed Viewer */}
      <div className="grid grid-cols-1 lg:grid-cols-4 gap-6">
        
        {/* Sidebar File List */}
        <div className="bg-slate-900/80 border border-slate-800 rounded-2xl p-4 space-y-2 font-mono text-xs">
          <div className="text-xs font-bold text-slate-400 uppercase tracking-wider mb-3 px-2">
            Workspace C++ Files
          </div>

          {CPP_FILES.map((f) => (
            <button
              key={f.filename}
              onClick={() => setSelectedFilename(f.filename)}
              className={`w-full flex items-center justify-between px-3 py-2.5 rounded-xl border transition-all text-left ${
                selectedFilename === f.filename
                  ? 'bg-cyan-500/20 border-cyan-500/40 text-cyan-300 font-bold shadow-sm'
                  : 'bg-slate-950/60 border-slate-800/80 text-slate-400 hover:text-slate-200 hover:bg-slate-900'
              }`}
            >
              <div className="flex items-center space-x-2 truncate">
                <FileCode className="w-4 h-4 text-cyan-400 shrink-0" />
                <span className="truncate">{f.filename}</span>
              </div>
              <span className={`text-[9px] px-1.5 py-0.5 rounded font-bold uppercase ${
                f.category === 'header' ? 'bg-cyan-500/20 text-cyan-300' :
                f.category === 'source' ? 'bg-emerald-500/20 text-emerald-300' :
                f.category === 'script' ? 'bg-amber-500/20 text-amber-300' :
                'bg-purple-500/20 text-purple-300'
              }`}>
                {f.category}
              </span>
            </button>
          ))}
        </div>

        {/* File Content Editor Display */}
        <div className="lg:col-span-3 bg-slate-900/80 border border-slate-800 rounded-2xl p-5 space-y-4">
          
          <div className="flex flex-col sm:flex-row sm:items-center justify-between gap-3 font-mono text-xs border-b border-slate-800/80 pb-3">
            <div>
              <div className="flex items-center space-x-2">
                <h3 className="text-sm font-bold text-slate-100">{selectedFile.filename}</h3>
                <span className="text-[10px] px-2 py-0.5 bg-slate-800 text-slate-300 rounded font-semibold">
                  {selectedFile.content.split('\n').length} Lines
                </span>
              </div>
              <p className="text-xs text-slate-400 mt-1">{selectedFile.description}</p>
            </div>

            <div className="flex items-center space-x-2">
              <button
                onClick={() => copyToClipboard(`cat << 'EOF' > ${selectedFile.filename}\n${selectedFile.content}\nEOF`, selectedFile.filename)}
                className="flex items-center space-x-1.5 px-3 py-1.5 rounded-lg bg-slate-800 border border-slate-700 hover:bg-slate-700 text-slate-200 font-bold transition-all"
              >
                {copiedFilename === selectedFile.filename ? <Check className="w-3.5 h-3.5 text-emerald-400" /> : <Copy className="w-3.5 h-3.5" />}
                <span>{copiedFilename === selectedFile.filename ? 'COPIED!' : 'COPY CAT BLOCK'}</span>
              </button>

              <button
                onClick={() => downloadFile(selectedFile.filename, selectedFile.content)}
                className="flex items-center space-x-1.5 px-3 py-1.5 rounded-lg bg-cyan-500/20 border border-cyan-500/40 hover:bg-cyan-500/30 text-cyan-300 font-bold transition-all"
              >
                <Download className="w-3.5 h-3.5" />
                <span>DOWNLOAD</span>
              </button>
            </div>
          </div>

          <div className="relative rounded-xl overflow-hidden border border-slate-800 bg-slate-950 p-4 font-mono text-xs text-slate-200 max-h-[500px] overflow-y-auto scrollbar-thin">
            <pre className="whitespace-pre">{selectedFile.content}</pre>
          </div>

        </div>

      </div>

    </div>
  );
};
