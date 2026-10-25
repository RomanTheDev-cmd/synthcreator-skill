// Временный режим --notes для main() в Tests/PowerBassTests.cpp (только во временной копии; в проект не внесён).
    if (argc > 3 && juce::String (argv[1]) == "--notes")
    {
        // Временный режим (только копия в scratchpad): argv[2] — файл "id=value ..." (одна строка),
        // argv[3] — файл нот "onSample offSample pitch velocity" построчно; argv[4] = "onfirst" — note-on раньше note-off
        // на одном отсчёте; argv[5] — размер блока. Пишет float32 стерео в stdout.
        Snapshot s;
        {
            std::ifstream pf (argv[2]); std::string tok;
            while (pf >> tok) { const auto eq = tok.find ('='); if (eq != std::string::npos) s[juce::String (tok.substr (0, eq))] = std::stof (tok.substr (eq + 1)); }
        }
        struct N { int on, off, pitch; float vel; };
        std::vector<N> ns; { std::ifstream nf (argv[3]); N n; while (nf >> n.on >> n.off >> n.pitch >> n.vel) ns.push_back (n); }
        const bool onFirst = argc > 4 && juce::String (argv[4]) == "onfirst";
        const int block = argc > 5 ? std::atoi (argv[5]) : 512;
        int total = 0; for (auto& n : ns) total = std::max (total, n.off); total += 48000;
        dsp::BassEngine e; const auto p = paramsFromSnapshot (s);
        e.prepare (48000.0, block, p);
        std::vector<float> l ((size_t) block), r ((size_t) block), out;
        for (int pos = 0; pos < total; pos += block)
        {
            juce::MidiBuffer m;
            for (int pass = 0; pass < 2; ++pass)
                for (auto& n : ns)
                {
                    const bool wantOn = (pass == 0) == onFirst;
                    const int t = wantOn ? n.on : n.off;
                    if (t >= pos && t < pos + block)
                        m.addEvent (wantOn ? juce::MidiMessage::noteOn (1, n.pitch, n.vel) : juce::MidiMessage::noteOff (1, n.pitch), t - pos);
                }
            e.process (l.data(), r.data(), block, m, p);
            for (int i = 0; i < block; ++i) { out.push_back (l[(size_t) i]); out.push_back (r[(size_t) i]); }
        }
        std::fwrite (out.data(), sizeof (float), out.size(), stdout);
        return 0;
    }
