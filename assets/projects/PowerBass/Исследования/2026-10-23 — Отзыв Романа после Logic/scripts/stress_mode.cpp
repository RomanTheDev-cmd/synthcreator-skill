// Временный режим --stress для main() в Tests/PowerBassTests.cpp (только во временной копии; в проект не внесён).
    if (argc > 1 && juce::String (argv[1]) == "--stress")
    {
        // Временный стресс (копия в scratchpad): пресеты сменяются каждые 120 мс во время ролла.
        const auto& all = factoryPresets();
        for (int block : { 64, 512 })
            for (int os : { 0, 1, 2 })
            {
                dsp::BassEngine e;
                const double sr = 48000.0;
                SynthParams p = paramsFromSnapshot (all[0].values); p.oversampleIndex = os;
                e.prepare (sr, block, p);
                const auto notes = patternNotes (33, 0, 133.0, 0.12 * (double) all.size() + 1.0);
                const int total = (int) ((0.12 * (double) all.size() + 3.0) * sr);
                std::vector<float> l ((size_t) block), r ((size_t) block);
                double worst = 0.0, sum = 0.0; int blocks = 0, bad = 0, worstAt = 0; size_t presetAt = 0;
                std::vector<double> rmsAfter (all.size(), 0.0);
                double tailPeak = 0.0;
                for (int pos = 0; pos < total; pos += block)
                {
                    const size_t want = std::min (all.size() - 1, (size_t) (pos / sr / 0.12));
                    if (want != presetAt) { presetAt = want; p = paramsFromSnapshot (all[want].values); p.oversampleIndex = (os + (int) want) % 3 == 2 ? 2 : os; }
                    juce::MidiBuffer m;
                    for (auto& n : notes)
                    {
                        const int on = (int) (n.on * sr), off = (int) (n.off * sr);
                        if (on >= pos && on < pos + block) m.addEvent (juce::MidiMessage::noteOn (1, n.note, n.vel), on - pos);
                        if (off >= pos && off < pos + block) m.addEvent (juce::MidiMessage::noteOff (1, n.note), off - pos);
                    }
                    const auto t0 = std::chrono::steady_clock::now();
                    e.process (l.data(), r.data(), block, m, p);
                    const double us = std::chrono::duration<double, std::micro> (std::chrono::steady_clock::now() - t0).count();
                    const double budget = block / sr * 1.0e6;
                    if (us / budget > worst) { worst = us / budget; worstAt = (int) presetAt; }
                    sum += us / budget; ++blocks;
                    for (int i = 0; i < block; ++i)
                    {
                        if (! std::isfinite (l[(size_t) i]) || ! std::isfinite (r[(size_t) i])) ++bad;
                        if (pos / sr < 0.12 * (double) all.size()) rmsAfter[presetAt] = std::max (rmsAfter[presetAt], (double) std::abs (l[(size_t) i]));
                        else if (pos / sr > 0.12 * (double) all.size() + 2.5) tailPeak = std::max (tailPeak, (double) std::abs (l[(size_t) i]));
                    }
                }
                int silent = 0; juce::String names;
                for (size_t i = 0; i < all.size(); ++i) if (rmsAfter[i] < 1.0e-3) { ++silent; names << all[i].name << "; "; }
                std::printf ("блок %3d, OS %d: худший блок %.0f%% бюджета (пресет %s), средний %.1f%%, NaN/inf %d, молчащих отрезков %d %s, хвост через 2,5 с %.5f\n",
                             block, os, worst * 100.0, all[(size_t) worstAt].name.toRawUTF8(), sum / blocks * 100.0, bad, silent, names.toRawUTF8(), tailPeak);
            }
        return 0;
    }
