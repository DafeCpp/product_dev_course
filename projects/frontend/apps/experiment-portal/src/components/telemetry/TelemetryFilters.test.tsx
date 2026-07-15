import { render, screen } from "@testing-library/react";
import userEvent from "@testing-library/user-event";
import { describe, expect, it, vi } from "vitest";
import TelemetryFilters from "./TelemetryFilters";

describe("TelemetryFilters", () => {
  it("forwards project selection and view mode changes", async () => {
    const user = userEvent.setup();
    const onProjectChange = vi.fn();
    const onViewModeChange = vi.fn();
    render(
      <TelemetryFilters
        projectId="p1"
        experimentId="e1"
        runId="r1"
        viewMode="live"
        projects={[
          { id: "p1", name: "Первый" },
          { id: "p2", name: "Другой" },
        ]}
        experiments={[{ id: "e1", name: "Эксперимент" }]}
        runs={[{ id: "r1", name: "Пуск" }]}
        projectsLoading={false}
        experimentsLoading={false}
        runsLoading={false}
        onProjectChange={onProjectChange}
        onExperimentChange={vi.fn()}
        onRunChange={vi.fn()}
        onViewModeChange={onViewModeChange}
      />,
    );
    await user.click(screen.getByLabelText("Проект"));
    await user.click(screen.getByRole("option", { name: "Другой" }));
    expect(onProjectChange).toHaveBeenCalledWith("p2");
    await user.click(screen.getByRole("switch", { name: "Live" }));
    expect(onViewModeChange).toHaveBeenCalledWith("history");
  });
});
