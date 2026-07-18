import { render, screen } from "@testing-library/react";
import userEvent from "@testing-library/user-event";
import { describe, expect, it, vi } from "vitest";
import HistorySensorControls from "./HistorySensorControls";
import type { Sensor } from "../../types";

describe("HistorySensorControls", () => {
  it("filters, adds and removes sensors", async () => {
    const user = userEvent.setup();
    const onFilterChange = vi.fn();
    const onAdd = vi.fn();
    const onRemove = vi.fn();
    const sensor = {
      id: "s1",
      name: "Температура",
      type: "temperature",
    } as Sensor;
    render(
      <HistorySensorControls
        sensors={[sensor]}
        selectedIds={["s1"]}
        filteredSensors={[sensor]}
        filter=""
        limitReached={false}
        onFilterChange={onFilterChange}
        onAdd={onAdd}
        onAddAll={vi.fn()}
        onClear={vi.fn()}
        onRemove={onRemove}
      />,
    );
    await user.type(screen.getByLabelText("Фильтр сенсоров"), "т");
    expect(onFilterChange).toHaveBeenCalledWith("т");
    await user.click(screen.getByLabelText("Сенсоры"));
    await user.click(screen.getByRole("option", { name: /температура/i }));
    expect(onAdd).toHaveBeenCalledWith("s1");
    await user.click(screen.getByLabelText("Удалить сенсор"));
    expect(onRemove).toHaveBeenCalledWith("s1");
  });
});
