#pragma once

#include <juce_core/juce_core.h>
#include "Analyzer.h"
#include <mutex>
#include <set>
#include <vector>

/**
    Lista de las instancias de Prisma abiertas en el mismo programa (el DAW carga el plugin una
    vez y todas las instancias comparten esta lista). Cada una publica su espectro de salida y su
    curva; así una puede mostrar la de otra pista y marcar dónde chocan ("collision detection").
    Todo se usa desde el hilo de mensajes, pero igual va con un mutex por las dudas.
*/
class InstanceRegistry
{
public:
    struct Entry
    {
        int id = 0;
        juce::String name;
        std::array<float, SpectrumAnalyzer::points> spectrum {};
        std::array<float, SpectrumAnalyzer::points> curve {};
        bool hasSpectrum = false;
        juce::uint32 updatedMs = 0;
    };

    static InstanceRegistry& get()
    {
        static InstanceRegistry instance;
        return instance;
    }

    int add()
    {
        std::lock_guard<std::mutex> lock(mutex);
        Entry e;
        e.id = nextId++;
        e.name = "Prisma " + juce::String(e.id);
        entries.push_back(e);
        return e.id;
    }

    void remove(int id)
    {
        std::lock_guard<std::mutex> lock(mutex);
        entries.erase(std::remove_if(entries.begin(), entries.end(), [id](const Entry& e) { return e.id == id; }), entries.end());
        watchers.erase(id);
    }

    void publish(int id, const juce::String& name, const float* spectrum, const float* curve)
    {
        std::lock_guard<std::mutex> lock(mutex);
        for (auto& e : entries)
            if (e.id == id)
            {
                if (name.isNotEmpty())
                    e.name = name;
                if (spectrum != nullptr)
                {
                    std::copy(spectrum, spectrum + SpectrumAnalyzer::points, e.spectrum.begin());
                    e.hasSpectrum = true;
                }
                if (curve != nullptr)
                    std::copy(curve, curve + SpectrumAnalyzer::points, e.curve.begin());
                e.updatedMs = juce::Time::getMillisecondCounter();
            }
    }

    std::vector<Entry> others(int selfId)
    {
        std::lock_guard<std::mutex> lock(mutex);
        std::vector<Entry> out;
        for (const auto& e : entries)
            if (e.id != selfId)
                out.push_back(e);
        return out;
    }

    bool find(int id, Entry& out)
    {
        std::lock_guard<std::mutex> lock(mutex);
        for (const auto& e : entries)
            if (e.id == id)
            {
                out = e;
                return true;
            }
        return false;
    }

    /** Una instancia está mirando a otras: todas tienen que publicar su espectro. */
    void setWatching(int id, bool watching)
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (watching)
            watchers.insert(id);
        else
            watchers.erase(id);
    }

    bool anyoneWatching()
    {
        std::lock_guard<std::mutex> lock(mutex);
        return !watchers.empty();
    }

private:
    std::mutex mutex;
    std::vector<Entry> entries;
    std::set<int> watchers;
    int nextId = 1;
};
