import { useEffect, useRef, useState } from "react";
import { SettingsIcon } from "../common";
import type { HistoryValueMode } from "../../hooks/useTelemetryHistory";

interface TelemetryHistorySettingsProps {
  includeLate: boolean;
  useAggregated: boolean;
  order: "asc" | "desc";
  valueMode: HistoryValueMode;
  maxPoints: number;
  onIncludeLateChange: (value: boolean) => void;
  onAggregatedChange: (value: boolean) => void;
  onOrderChange: (value: "asc" | "desc") => void;
  onValueModeChange: (value: HistoryValueMode) => void;
  onMaxPointsChange: (value: number) => void;
}
export default function TelemetryHistorySettings(
  props: TelemetryHistorySettingsProps,
) {
  const [open, setOpen] = useState(false);
  const ref = useRef<HTMLDivElement>(null);
  useEffect(() => {
    const close = (event: MouseEvent) => {
      if (ref.current && !ref.current.contains(event.target as Node))
        setOpen(false);
    };
    if (open) document.addEventListener("mousedown", close);
    return () => document.removeEventListener("mousedown", close);
  }, [open]);
  return (
    <div className="history-settings-wrap" ref={ref}>
      <button
        type="button"
        className="pill-btn"
        aria-expanded={open}
        onClick={() => setOpen((value) => !value)}
      >
        <SettingsIcon />
        Настройки
      </button>
      {open && (
        <div
          className="history-settings-popover"
          role="dialog"
          aria-label="Дополнительные настройки"
        >
          <span className="hsp-arrow" />
          <div className="hsp-section-title">Данные</div>
          <Toggle
            label="Включать поздние точки"
            checked={props.includeLate}
            disabled={props.useAggregated}
            onChange={props.onIncludeLateChange}
          />
          <Toggle
            label="Агрегация 1 мин (avg / min / max)"
            checked={props.useAggregated}
            onChange={props.onAggregatedChange}
          />
          <div className="hsp-section-title">Отображение</div>
          <Segment
            label="Порядок"
            name="hsp-order"
            value={props.order}
            options={[
              ["asc", "от начала"],
              ["desc", "последние"],
            ]}
            onChange={props.onOrderChange}
          />
          <Segment
            label="Значения"
            name="hsp-value-mode"
            value={props.valueMode}
            options={[
              ["physical", "physical"],
              ["raw", "raw"],
            ]}
            onChange={props.onValueModeChange}
          />
          <div className="hsp-section-title">Данные</div>
          <div className="hsp-row">
            <span className="hsp-name">Максимум точек</span>
            <input
              type="number"
              min={100}
              max={20000}
              className="hsp-number-input"
              value={props.maxPoints}
              onChange={(event) =>
                props.onMaxPointsChange(Number(event.target.value || 5000))
              }
            />
          </div>
        </div>
      )}
    </div>
  );
}
function Toggle({
  label,
  checked,
  disabled,
  onChange,
}: {
  label: string;
  checked: boolean;
  disabled?: boolean;
  onChange: (value: boolean) => void;
}) {
  return (
    <div className="hsp-row">
      <span className="hsp-name">{label}</span>
      <label className="history-toggle">
        <input
          type="checkbox"
          checked={checked}
          disabled={disabled}
          onChange={(event) => onChange(event.target.checked)}
        />
      </label>
    </div>
  );
}
function Segment<T extends string>({
  label,
  name,
  value,
  options,
  onChange,
}: {
  label: string;
  name: string;
  value: T;
  options: [T, string][];
  onChange: (value: T) => void;
}) {
  return (
    <div className="hsp-row">
      <span className="hsp-name">{label}</span>
      <div className="history-seg">
        {options.map(([option, text]) => (
          <label key={option} className={value === option ? "on" : ""}>
            <input
              type="radio"
              name={name}
              checked={value === option}
              onChange={() => onChange(option)}
            />
            <span>{text}</span>
          </label>
        ))}
      </div>
    </div>
  );
}
