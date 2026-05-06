import Autocomplete from '@mui/material/Autocomplete'
import TextField from '@mui/material/TextField'
import CircularProgress from '@mui/material/CircularProgress'
import type { UserSearchResult } from '../../types'
import type { Role } from '../../types/permissions'
import { MaterialSelect } from '../common'

interface AddMemberFormProps {
    selectedUser: UserSearchResult | null
    userSearchInput: string
    userOptions: UserSearchResult[]
    isSearchingUsers: boolean
    debouncedQuery: string
    newMemberRoleId: string
    projectRoles: Role[]
    isPending: boolean
    isAddPending: boolean
    onUserInputChange: (value: string) => void
    onUserSelect: (value: UserSearchResult | null) => void
    onRoleChange: (value: string) => void
    onSubmit: (e: React.FormEvent) => void
}

function AddMemberForm({
    selectedUser,
    userSearchInput,
    userOptions,
    isSearchingUsers,
    debouncedQuery,
    newMemberRoleId,
    projectRoles,
    isPending,
    isAddPending,
    onUserInputChange,
    onUserSelect,
    onRoleChange,
    onSubmit,
}: AddMemberFormProps) {
    return (
        <div className="add-member-form">
            <h3>Добавить участника</h3>
            <form onSubmit={onSubmit}>
                <div className="form-group">
                    <label htmlFor="new_member_user_id">
                        Пользователь <span className="required">*</span>
                    </label>
                    <Autocomplete<UserSearchResult>
                        id="new_member_user_id"
                        options={userOptions}
                        value={selectedUser}
                        inputValue={userSearchInput}
                        onInputChange={(_event, value) => {
                            onUserInputChange(value)
                            if (!value) onUserSelect(null)
                        }}
                        onChange={(_event, value) => onUserSelect(value)}
                        getOptionLabel={(option) => option.username}
                        isOptionEqualToValue={(option, value) => option.id === value.id}
                        renderOption={(props, option) => (
                            <li {...props} key={option.id}>
                                <span style={{ fontWeight: 500 }}>{option.username}</span>
                                <span style={{ marginLeft: 8, color: '#888', fontSize: '0.85em' }}>
                                    {option.email}
                                </span>
                            </li>
                        )}
                        loading={isSearchingUsers}
                        noOptionsText={
                            debouncedQuery.length < 2
                                ? 'Введите минимум 2 символа'
                                : 'Пользователи не найдены'
                        }
                        disabled={isPending}
                        renderInput={(params) => (
                            <TextField
                                {...params}
                                placeholder="Поиск по имени или email"
                                size="small"
                                InputProps={{
                                    ...params.InputProps,
                                    endAdornment: (
                                        <>
                                            {isSearchingUsers ? (
                                                <CircularProgress color="inherit" size={16} />
                                            ) : null}
                                            {params.InputProps.endAdornment}
                                        </>
                                    ),
                                }}
                            />
                        )}
                        filterOptions={(x) => x}
                    />
                </div>

                <div className="form-group">
                    <label htmlFor="new_member_role">Роль</label>
                    {projectRoles.length > 0 ? (
                        <MaterialSelect
                            id="new_member_role"
                            value={newMemberRoleId}
                            onChange={(value) => onRoleChange(value)}
                            disabled={isPending}
                        >
                            {projectRoles.map((role) => (
                                <option key={role.id} value={role.id}>
                                    {role.name}
                                    {role.is_builtin ? '' : ' (кастомная)'}
                                </option>
                            ))}
                        </MaterialSelect>
                    ) : (
                        <MaterialSelect
                            id="new_member_role"
                            value="viewer"
                            onChange={() => {}}
                            disabled={isPending}
                        >
                            <option value="viewer">Наблюдатель</option>
                            <option value="editor">Редактор</option>
                            <option value="owner">Владелец</option>
                        </MaterialSelect>
                    )}
                </div>

                <button
                    type="submit"
                    className="btn btn-primary"
                    disabled={isPending || !selectedUser}
                >
                    {isAddPending ? 'Добавление...' : 'Добавить участника'}
                </button>
            </form>
        </div>
    )
}

export default AddMemberForm
