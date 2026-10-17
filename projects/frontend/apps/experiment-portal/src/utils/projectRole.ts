import type { ProjectMember } from '../types'

export type BuiltinProjectRole = 'owner' | 'editor' | 'viewer'

const BUILTIN_ROLES: readonly BuiltinProjectRole[] = ['owner', 'editor', 'viewer']

/** Strongest built-in role among the role names of a project member (API sends `roles: string[]`). */
export function getMemberBuiltinRole(member: Pick<ProjectMember, 'roles'>): BuiltinProjectRole | undefined {
  const names = member.roles.map((name) => name.toLowerCase())
  return BUILTIN_ROLES.find((role) => names.includes(role))
}
