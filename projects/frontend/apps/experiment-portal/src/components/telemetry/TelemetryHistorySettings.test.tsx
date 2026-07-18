import { fireEvent, render, screen } from "@testing-library/react";
import { useState } from "react";
import userEvent from "@testing-library/user-event";
import { describe, expect, it, vi } from "vitest";
import TelemetryHistorySettings from "./TelemetryHistorySettings";

describe("TelemetryHistorySettings", () => {
  it("updates history settings from the popover", async () => {
    const user = userEvent.setup();
    const onOrderChange = vi.fn();
    const onMaxPointsChange = vi.fn();
    function SettingsHarness() {
      const [maxPoints, setMaxPoints] = useState(5000);
      return (
        <TelemetryHistorySettings
          includeLate
          useAggregated={false}
          order="asc"
          valueMode="physical"
          maxPoints={maxPoints}
          onIncludeLateChange={vi.fn()}
          onAggregatedChange={vi.fn()}
          onOrderChange={onOrderChange}
          onValueModeChange={vi.fn()}
          onMaxPointsChange={(value) => {
            onMaxPointsChange(value);
            setMaxPoints(value);
          }}
        />
      );
    }
    render(<SettingsHarness />);
    await user.click(screen.getByRole("button", { name: "Настройки" }));
    await user.click(screen.getByLabelText("последние"));
    expect(onOrderChange).toHaveBeenCalledWith("desc");
    fireEvent.change(screen.getByRole("spinbutton"), {
      target: { value: "1000" },
    });
    expect(onMaxPointsChange).toHaveBeenLastCalledWith(1000);
  });
});
