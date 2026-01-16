import { Routes, Route, Navigate } from 'react-router-dom'
import Layout from './components/Layout'
import ProtectedRoute from './components/ProtectedRoute'
import Login from './pages/Login'
import Register from './pages/Register'
import ExperimentsList from './pages/ExperimentsList'
import ExperimentDetail from './pages/ExperimentDetail'
import RunDetail from './pages/RunDetail'
import SensorsList from './pages/SensorsList'
import CreateSensor from './pages/CreateSensor'
import ProjectsList from './pages/ProjectsList'
import TelemetryViewer from './pages/TelemetryViewer'
import './App.css'

function App() {
  return (
    <Routes>
      <Route path="/login" element={<Login />} />
      <Route path="/register" element={<Register />} />
      <Route
        path="/*"
        element={
          <ProtectedRoute>
            <Layout>
              <Routes>
                <Route path="/" element={<Navigate to="/experiments" replace />} />
                <Route path="/projects" element={<ProjectsList />} />
                <Route path="/experiments" element={<ExperimentsList />} />
                <Route path="/experiments/:id" element={<ExperimentDetail />} />
                <Route path="/runs/:id" element={<RunDetail />} />
                <Route path="/sensors" element={<SensorsList />} />
                <Route path="/sensors/new" element={<CreateSensor />} />
                <Route path="/telemetry" element={<TelemetryViewer />} />
              </Routes>
            </Layout>
          </ProtectedRoute>
        }
      />
    </Routes>
  )
}

export default App

