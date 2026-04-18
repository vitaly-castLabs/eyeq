#pragma once

#include <cstdio>
#include <cstdlib>
#include <optional>
#include <string>
#include <unistd.h>
#include <sys/wait.h>
#include <vector>

#include "metrics.h"

class Ssimulacra2 : public Metrics {
public:
    using Metrics::Metrics;

    const char* name() const override { return "SSIMULACRA2"; }

    std::optional<std::vector<Score>> measure(const Image& ref, const Image& dist) noexcept override {
        if (ref.path.empty() || dist.path.empty())
            return std::nullopt;

        int pipefd[2];
        if (pipe(pipefd) != 0)
            return std::nullopt;

        pid_t pid = fork();
        if (pid < 0) {
            close(pipefd[0]);
            close(pipefd[1]);
            return std::nullopt;
        }

        if (pid == 0) {
            close(pipefd[0]);
            dup2(pipefd[1], STDOUT_FILENO);
            close(pipefd[1]);
            char bin[] = SSIMULACRA2_BIN;
            std::string r = ref.path, d = dist.path;
            char* const argv[] = {bin, r.data(), d.data(), nullptr};
            execv(bin, argv);
            _exit(127);
        }

        close(pipefd[1]);
        std::string out;
        char buf[256];
        ssize_t n;
        while ((n = read(pipefd[0], buf, sizeof(buf))) > 0)
            out.append(buf, static_cast<size_t>(n));
        close(pipefd[0]);

        int status = 0;
        waitpid(pid, &status, 0);
        if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
            return std::nullopt;

        try {
            return std::vector<Score>{{"SSIMULACRA2", std::stof(out)}};
        } catch (...) {
            return std::nullopt;
        }
    }
};
