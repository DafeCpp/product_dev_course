import { screen, within, waitFor } from '@testing-library/react'
import type { UserEvent } from '@testing-library/user-event'

/**
 * Helper to interact with `MaterialSelect` in tests.
 * - For `multiple` selects it renders a real <select> and we use user.selectOptions.
 * - For single selects it renders a trigger <button> + listbox of option buttons.
 */
export async function pickMaterialSelectOption(
    user: UserEvent,
    label: RegExp | string,
    option: RegExp | string
) {
    const control = screen.getByLabelText(label)

    if (control instanceof HTMLSelectElement) {
        // Multiple mode renders a native select.
        await user.selectOptions(control, option as any)
        return
    }

    await user.click(control)

    const listboxId = control.getAttribute('aria-controls')
    const listbox = listboxId
        ? await waitFor(() => {
              const el = document.getElementById(listboxId) as HTMLElement | null
              if (!el) throw new Error('MaterialSelect listbox not found')
              return el
          })
        : await screen.findByRole('listbox')

    const optionEl = within(listbox).getByRole('option', { name: option })
    await user.click(optionEl)
}

